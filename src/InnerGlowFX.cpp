/*
	InnerGlowFX.cpp

	Entry point, parameter setup and command dispatch.

	The glow is a neighbourhood effect: a pixel's result depends on how far it
	sits from the nearest transparent pixel, up to Size pixels away. That drives
	two things that a per-pixel effect would not need. PF_Cmd_SMART_PRE_RENDER
	asks After Effects for an input rect grown by the glow's reach, and the
	render builds a coverage field across that whole rect before it touches a
	single output pixel.
*/

#include "InnerGlowFX.h"
#include "GlowRender.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <vector>

namespace {

/* ------------------------------------------------------------------------ */
/* Suite access                                                             */
/* ------------------------------------------------------------------------ */

template <typename SuiteT>
class SuiteScoper {
public:
	SuiteScoper(PF_InData *in_data, const char *nameZ, int version)
		: mBasicP(in_data ? in_data->pica_basicP : NULL),
		  mNameZ(nameZ),
		  mVersion(version),
		  mSuiteP(NULL)
	{
		if (mBasicP) {
			const void *suiteP = NULL;
			if (mBasicP->AcquireSuite(mNameZ, mVersion, &suiteP) == kSPNoError) {
				mSuiteP = reinterpret_cast<const SuiteT*>(suiteP);
			}
		}
	}

	~SuiteScoper()
	{
		if (mSuiteP && mBasicP) {
			mBasicP->ReleaseSuite(mNameZ, mVersion);
		}
	}

	const SuiteT *operator->() const	{ return mSuiteP; }
	bool Valid() const					{ return mSuiteP != NULL; }

private:
	SuiteScoper(const SuiteScoper &);
	SuiteScoper &operator=(const SuiteScoper &);

	SPBasicSuite	*mBasicP;
	const char		*mNameZ;
	int				mVersion;
	const SuiteT	*mSuiteP;
};

/* ------------------------------------------------------------------------ */
/* Resolved settings                                                        */
/* ------------------------------------------------------------------------ */

struct RenderContext {
	igfx::GlowSettings		glow;
	igfx::CompositeSettings	composite;
};

inline float FixedToFloat(PF_Fixed value)
{
	return static_cast<float>(value) / 65536.0f;
}

inline float RationalToFloat(const PF_RationalScale &scale)
{
	if (scale.den == 0) {
		return 1.0f;
	}
	return static_cast<float>(scale.num) / static_cast<float>(scale.den);
}

/* Size is authored in full resolution pixels, so it has to follow the
   downsample factor or the glow would grow as the preview resolution drops. */
inline float DownsampleScale(PF_InData *in_data)
{
	const float sx = RationalToFloat(in_data->downsample_x);
	const float sy = RationalToFloat(in_data->downsample_y);
	return (sx + sy) * 0.5f;
}

igfx::Rgb ResolveColor(PF_InData *in_data, const PF_ParamDef *color_defP)
{
	igfx::Rgb out;

	/* The colour suite reports the swatch in the effect's working colour space,
	   which is what the pixels are in. Falling back to a plain 0..255 divide
	   only matters on hosts too old to offer the suite. */
	SuiteScoper<PF_ColorParamSuite1> colorSuite(in_data, kPFColorParamSuite,
												kPFColorParamSuiteVersion1);

	if (colorSuite.Valid()) {
		PF_PixelFloat floatColor;
		AEFX_CLR_STRUCT(floatColor);

		if (colorSuite->PF_GetFloatingPointColorFromColorDef(in_data->effect_ref,
															color_defP,
															&floatColor) == PF_Err_NONE) {
			out.r = static_cast<float>(floatColor.red);
			out.g = static_cast<float>(floatColor.green);
			out.b = static_cast<float>(floatColor.blue);
			return out;
		}
	}

	out.r = static_cast<float>(color_defP->u.cd.value.red) / 255.0f;
	out.g = static_cast<float>(color_defP->u.cd.value.green) / 255.0f;
	out.b = static_cast<float>(color_defP->u.cd.value.blue) / 255.0f;
	return out;
}

void ResolveContext(
	PF_InData			*in_data,
	PF_ParamDef			*params[],
	RenderContext		&ctx)
{
	const float downsample = DownsampleScale(in_data);

	ctx.glow.sizePx = static_cast<float>(params[IGFX_SIZE]->u.fs_d.value) * downsample;
	ctx.glow.choke = static_cast<float>(params[IGFX_CHOKE]->u.fs_d.value) * 0.01f;
	ctx.glow.range = static_cast<float>(params[IGFX_RANGE]->u.fs_d.value) * 0.01f;
	ctx.glow.noise = static_cast<float>(params[IGFX_NOISE]->u.fs_d.value) * 0.01f;
	ctx.glow.technique = params[IGFX_TECHNIQUE]->u.pd.value;
	ctx.glow.source = params[IGFX_SOURCE]->u.pd.value;

	/* Filled in by RenderGlow, which is the first place that knows where the
	   buffer sits in the layer. */
	ctx.glow.originX = 0;
	ctx.glow.originY = 0;

	const float opacity = static_cast<float>(params[IGFX_OPACITY]->u.fs_d.value) * 0.01f;
	const float colorOpacity =
		static_cast<float>(params[IGFX_COLOR_OPACITY]->u.fs_d.value) * 0.01f;

	ctx.composite.color = ResolveColor(in_data, params[IGFX_COLOR]);
	ctx.composite.amount = opacity * colorOpacity;
	ctx.composite.blendMode = params[IGFX_BLEND_MODE]->u.pd.value;
}

/* How far outside the requested output rect the input has to reach for the
   glow to be correct at the edges of a tile. */
void ResolveMargins(PF_InData *in_data, float sizeFullRes, A_long &marginX, A_long &marginY)
{
	const float sizeX = sizeFullRes * RationalToFloat(in_data->downsample_x);
	const float sizeY = sizeFullRes * RationalToFloat(in_data->downsample_y);

	marginX = static_cast<A_long>(std::ceil(sizeX)) + 2;
	marginY = static_cast<A_long>(std::ceil(sizeY)) + 2;
}

/* ------------------------------------------------------------------------ */
/* Pixel access                                                             */
/* ------------------------------------------------------------------------ */

template <typename PixelT>
inline const PixelT *ConstRow(const PF_EffectWorld *worldP, int y)
{
	return reinterpret_cast<const PixelT*>(
		reinterpret_cast<const char*>(worldP->data) +
		static_cast<size_t>(y) * worldP->rowbytes);
}

template <typename PixelT>
inline PixelT *MutableRow(PF_EffectWorld *worldP, int y)
{
	return reinterpret_cast<PixelT*>(
		reinterpret_cast<char*>(worldP->data) +
		static_cast<size_t>(y) * worldP->rowbytes);
}

template <typename PixelT>
void ExtractAlpha(const PF_EffectWorld *worldP, float maxValue, float *alphaP)
{
	const int width = worldP->width;
	const int height = worldP->height;
	const float inverseMax = 1.0f / maxValue;

	for (int y = 0; y < height; ++y) {
		const PixelT *rowP = ConstRow<PixelT>(worldP, y);
		float *outP = alphaP + static_cast<size_t>(y) * width;

		for (int x = 0; x < width; ++x) {
			float a = static_cast<float>(rowP[x].alpha) * inverseMax;
			if (a < 0.0f) {
				a = 0.0f;
			} else if (a > 1.0f) {
				a = 1.0f;
			}
			outP[x] = a;
		}
	}
}

/*
	Writes the glow into the output world.

	offsetX / offsetY map an output pixel onto the input world, which is the
	larger of the two whenever the glow needed margin.
	layerOriginX / layerOriginY are the layer coordinates of output pixel (0,0),
	used only to pin the Dissolve dither and the Noise pattern to the layer.
*/
template <typename PixelT>
void CompositeWorld(
	const PF_EffectWorld	*inputP,
	PF_EffectWorld			*outputP,
	int						offsetX,
	int						offsetY,
	int						layerOriginX,
	int						layerOriginY,
	const float				*fieldP,
	const RenderContext		&ctx,
	float					maxValue,
	bool					clampResult)
{
	const int outWidth = outputP->width;
	const int outHeight = outputP->height;
	const int inWidth = inputP->width;
	const int inHeight = inputP->height;
	const float inverseMax = 1.0f / maxValue;

	for (int y = 0; y < outHeight; ++y) {
		PixelT *outRowP = MutableRow<PixelT>(outputP, y);

		const int iy = y + offsetY;
		const bool rowInside = (iy >= 0 && iy < inHeight);
		const PixelT *inRowP = rowInside ? ConstRow<PixelT>(inputP, iy) : NULL;
		const float *fieldRowP = rowInside
			? fieldP + static_cast<size_t>(iy) * inWidth
			: NULL;

		for (int x = 0; x < outWidth; ++x) {
			const int ix = x + offsetX;

			if (!rowInside || ix < 0 || ix >= inWidth) {
				/* Outside the checked out input there is nothing to composite
				   against, which can only mean fully transparent. */
				outRowP[x].alpha = 0;
				outRowP[x].red = 0;
				outRowP[x].green = 0;
				outRowP[x].blue = 0;
				continue;
			}

			const PixelT &inPixel = inRowP[ix];
			const float alpha = static_cast<float>(inPixel.alpha) * inverseMax;

			/* Alpha is passed straight through, always. Fully transparent
			   pixels keep whatever RGB they arrived with, so nothing that
			   depends on preserved RGB behind zero alpha gets trampled. */
			if (alpha <= 0.0f) {
				outRowP[x] = inPixel;
				continue;
			}

			/* Unpremultiply, do the colour maths straight, premultiply back.
			   Skipping this is what makes overlays darken and fringe along
			   antialiased edges. */
			const float inverseAlpha = 1.0f / alpha;

			igfx::Rgb base;
			base.r = static_cast<float>(inPixel.red) * inverseMax * inverseAlpha;
			base.g = static_cast<float>(inPixel.green) * inverseMax * inverseAlpha;
			base.b = static_cast<float>(inPixel.blue) * inverseMax * inverseAlpha;

			const igfx::Rgb shaded = igfx::CompositePixel(ctx.composite,
														  base,
														  fieldRowP[ix],
														  layerOriginX + x,
														  layerOriginY + y);

			float r = shaded.r * alpha * maxValue;
			float g = shaded.g * alpha * maxValue;
			float b = shaded.b * alpha * maxValue;

			if (clampResult) {
				r = std::min(std::max(r, 0.0f), maxValue);
				g = std::min(std::max(g, 0.0f), maxValue);
				b = std::min(std::max(b, 0.0f), maxValue);
			}

			outRowP[x].alpha = inPixel.alpha;
			outRowP[x].red = static_cast<decltype(inPixel.red)>(clampResult ? (r + 0.5f) : r);
			outRowP[x].green = static_cast<decltype(inPixel.green)>(clampResult ? (g + 0.5f) : g);
			outRowP[x].blue = static_cast<decltype(inPixel.blue)>(clampResult ? (b + 0.5f) : b);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* The render core, shared by the legacy and smart paths                    */
/* ------------------------------------------------------------------------ */

PF_Err ResolvePixelFormat(
	PF_InData				*in_data,
	const PF_EffectWorld	*worldP,
	PF_PixelFormat			&format)
{
	SuiteScoper<PF_WorldSuite2> worldSuite(in_data, kPFWorldSuite, kPFWorldSuiteVersion2);

	if (!worldSuite.Valid()) {
		return PF_Err_BAD_CALLBACK_PARAM;
	}

	return worldSuite->PF_GetPixelFormat(const_cast<PF_EffectWorld*>(worldP), &format);
}

PF_Err RenderGlow(
	PF_InData			*in_data,
	const PF_EffectWorld *inputP,
	PF_EffectWorld		*outputP,
	int					offsetX,
	int					offsetY,
	int					layerOriginX,
	int					layerOriginY,
	int					inputOriginX,
	int					inputOriginY,
	const RenderContext	&ctx)
{
	PF_Err			err = PF_Err_NONE;
	PF_PixelFormat	format = PF_PixelFormat_INVALID;

	ERR(ResolvePixelFormat(in_data, inputP, format));
	if (err) {
		return err;
	}

	const size_t pixelCount =
		static_cast<size_t>(inputP->width) * static_cast<size_t>(inputP->height);

	if (pixelCount == 0 || outputP->width <= 0 || outputP->height <= 0) {
		return PF_Err_NONE;
	}

	std::vector<float> alpha;
	std::vector<float> field;
	std::vector<float> scratch;

	try {
		alpha.resize(pixelCount);
		field.resize(pixelCount);
		scratch.resize(pixelCount);
	} catch (const std::bad_alloc &) {
		return PF_Err_OUT_OF_MEMORY;
	}

	switch (format) {
		case PF_PixelFormat_ARGB32:
			ExtractAlpha<PF_Pixel8>(inputP, static_cast<float>(PF_MAX_CHAN8), alpha.data());
			break;
		case PF_PixelFormat_ARGB64:
			ExtractAlpha<PF_Pixel16>(inputP, static_cast<float>(PF_MAX_CHAN16), alpha.data());
			break;
		case PF_PixelFormat_ARGB128:
			ExtractAlpha<PF_PixelFloat>(inputP, 1.0f, alpha.data());
			break;
		default:
			return PF_Err_BAD_CALLBACK_PARAM;
	}

	/* The field covers the input world, so the noise dither is keyed to where
	   that world sits in the layer. */
	igfx::GlowSettings glow = ctx.glow;
	glow.originX = inputOriginX;
	glow.originY = inputOriginY;

	igfx::BuildGlowField(alpha.data(),
						 inputP->width,
						 inputP->height,
						 glow,
						 field.data(),
						 scratch.data());

	switch (format) {
		case PF_PixelFormat_ARGB32:
			CompositeWorld<PF_Pixel8>(inputP, outputP, offsetX, offsetY,
									  layerOriginX, layerOriginY, field.data(), ctx,
									  static_cast<float>(PF_MAX_CHAN8), true);
			break;
		case PF_PixelFormat_ARGB64:
			CompositeWorld<PF_Pixel16>(inputP, outputP, offsetX, offsetY,
									   layerOriginX, layerOriginY, field.data(), ctx,
									   static_cast<float>(PF_MAX_CHAN16), true);
			break;
		case PF_PixelFormat_ARGB128:
			/* 32 bpc keeps overrange values, so the result is not clamped. */
			CompositeWorld<PF_PixelFloat>(inputP, outputP, offsetX, offsetY,
										  layerOriginX, layerOriginY, field.data(), ctx,
										  1.0f, false);
			break;
		default:
			return PF_Err_BAD_CALLBACK_PARAM;
	}

	return PF_Err_NONE;
}

/* ------------------------------------------------------------------------ */
/* Rect helpers                                                             */
/* ------------------------------------------------------------------------ */

bool IsEmptyRect(const PF_LRect *rectP)
{
	return (rectP->left >= rectP->right) || (rectP->top >= rectP->bottom);
}

void UnionRect(const PF_LRect *srcP, PF_LRect *dstP)
{
	if (IsEmptyRect(dstP)) {
		*dstP = *srcP;
	} else if (!IsEmptyRect(srcP)) {
		dstP->left = MIN(dstP->left, srcP->left);
		dstP->top = MIN(dstP->top, srcP->top);
		dstP->right = MAX(dstP->right, srcP->right);
		dstP->bottom = MAX(dstP->bottom, srcP->bottom);
	}
}

}	// namespace

/* ------------------------------------------------------------------------ */
/* Command handlers                                                         */
/* ------------------------------------------------------------------------ */

static PF_Err
About(
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output)
{
	PF_SPRINTF(out_data->return_msg,
			   "%s %d.%d\r\r%s",
			   STR_NAME,
			   MAJOR_VERSION,
			   MINOR_VERSION,
			   STR_DESCRIPTION);

	return PF_Err_NONE;
}

/*
	!!  KEEP IN SYNC WITH resources/InnerGlowFX_PiPL.r  !!

	The out_flags and out_flags2 set below must match AE_Effect_Global_OutFlags
	and AE_Effect_Global_OutFlags_2 in the PiPL exactly. After Effects reads the
	PiPL values when it scans the plug-in and never warns when the two disagree;
	it just misbehaves, quietly. The matching warning comment and the bit-by-bit
	breakdown live at the top of the .r file.

	  out_flags  = 0x02000000
	  out_flags2 = 0x08001400

	PF_OutFlag_PIX_INDEPENDENT is deliberately absent. A pixel's glow depends on
	how far it sits from the nearest transparent pixel, so results are not
	independent of their neighbours, and claiming otherwise would let After
	Effects render alternate rows and interpolate the rest.
*/
static PF_Err
GlobalSetup(
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output)
{
	out_data->my_version = PF_VERSION(MAJOR_VERSION,
									  MINOR_VERSION,
									  BUG_VERSION,
									  STAGE_VERSION,
									  BUILD_VERSION);

	out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;

	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
						   PF_OutFlag2_FLOAT_COLOR_AWARE |
						   PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

	return PF_Err_NONE;
}

static PF_Err
GlobalSetdown(
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output)
{
	/* Nothing is allocated at global setup, so there is nothing to release.
	   Every render allocates and frees its own buffers, which is what keeps the
	   effect safe under multi frame rendering. */
	return PF_Err_NONE;
}

static PF_Err
ParamsSetup(
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output)
{
	PF_ParamDef	def;

	PF_ADD_POPUPX("Blend Mode",
				  IGFX_BLEND_MODE_COUNT,
				  IGFX_BLEND_MODE_DFLT,
				  IGFX_BLEND_MODE_ITEMS,
				  0,
				  IGFX_BLEND_MODE);

	PF_ADD_FLOAT_SLIDERX("Opacity",
						 IGFX_OPACITY_MIN, IGFX_OPACITY_MAX,
						 IGFX_OPACITY_MIN, IGFX_OPACITY_MAX,
						 IGFX_OPACITY_DFLT,
						 PF_Precision_TENTHS,
						 PF_ValueDisplayFlag_PERCENT,
						 0,
						 IGFX_OPACITY);

	PF_ADD_FLOAT_SLIDERX("Noise",
						 IGFX_NOISE_MIN, IGFX_NOISE_MAX,
						 IGFX_NOISE_MIN, IGFX_NOISE_MAX,
						 IGFX_NOISE_DFLT,
						 PF_Precision_TENTHS,
						 PF_ValueDisplayFlag_PERCENT,
						 0,
						 IGFX_NOISE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Color",
				 IGFX_COLOR_RED_DFLT,
				 IGFX_COLOR_GREEN_DFLT,
				 IGFX_COLOR_BLUE_DFLT,
				 IGFX_COLOR);

	PF_ADD_FLOAT_SLIDERX("Color Opacity",
						 IGFX_COLOR_OPACITY_MIN, IGFX_COLOR_OPACITY_MAX,
						 IGFX_COLOR_OPACITY_MIN, IGFX_COLOR_OPACITY_MAX,
						 IGFX_COLOR_OPACITY_DFLT,
						 PF_Precision_TENTHS,
						 PF_ValueDisplayFlag_PERCENT,
						 0,
						 IGFX_COLOR_OPACITY);

	PF_ADD_POPUPX("Technique",
				  IGFX_TECHNIQUE_COUNT,
				  IGFX_TECHNIQUE_DFLT,
				  IGFX_TECHNIQUE_ITEMS,
				  0,
				  IGFX_TECHNIQUE);

	PF_ADD_POPUPX("Source",
				  IGFX_SOURCE_COUNT,
				  IGFX_SOURCE_DFLT,
				  IGFX_SOURCE_ITEMS,
				  0,
				  IGFX_SOURCE);

	PF_ADD_FLOAT_SLIDERX("Choke",
						 IGFX_CHOKE_MIN, IGFX_CHOKE_MAX,
						 IGFX_CHOKE_MIN, IGFX_CHOKE_MAX,
						 IGFX_CHOKE_DFLT,
						 PF_Precision_TENTHS,
						 PF_ValueDisplayFlag_PERCENT,
						 0,
						 IGFX_CHOKE);

	PF_ADD_FLOAT_SLIDERX("Size",
						 IGFX_SIZE_MIN, IGFX_SIZE_MAX,
						 IGFX_SIZE_MIN, 100.0,
						 IGFX_SIZE_DFLT,
						 PF_Precision_TENTHS,
						 PF_ValueDisplayFlag_NONE,
						 0,
						 IGFX_SIZE);

	PF_ADD_FLOAT_SLIDERX("Range",
						 IGFX_RANGE_MIN, IGFX_RANGE_MAX,
						 IGFX_RANGE_MIN, IGFX_RANGE_MAX,
						 IGFX_RANGE_DFLT,
						 PF_Precision_TENTHS,
						 PF_ValueDisplayFlag_PERCENT,
						 0,
						 IGFX_RANGE);

	out_data->num_params = IGFX_NUM_PARAMS;

	return PF_Err_NONE;
}

/* Legacy path, used by hosts and situations that do not drive smart render. */
static PF_Err
Render(
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output)
{
	RenderContext ctx;
	ResolveContext(in_data, params, ctx);

	/* output_origin is the position of the input buffer's top left corner
	   inside the output buffer, so it inverts to map the other way. */
	return RenderGlow(in_data,
					  &params[IGFX_INPUT]->u.ld,
					  output,
					  -in_data->output_origin_x,
					  -in_data->output_origin_y,
					  0,
					  0,
					  0,
					  0,
					  ctx);
}

static PF_Err
PreRender(
	PF_InData			*in_data,
	PF_OutData			*out_data,
	PF_PreRenderExtra	*extra)
{
	PF_Err				err = PF_Err_NONE;
	PF_Err				err2 = PF_Err_NONE;
	PF_RenderRequest	req = extra->input->output_request;
	PF_CheckoutResult	in_result;
	PF_ParamDef			size_param;

	AEFX_CLR_STRUCT(in_result);
	AEFX_CLR_STRUCT(size_param);

	ERR(PF_CHECKOUT_PARAM(in_data,
						  IGFX_SIZE,
						  in_data->current_time,
						  in_data->time_step,
						  in_data->time_scale,
						  &size_param));

	if (!err) {
		A_long marginX = 0;
		A_long marginY = 0;

		ResolveMargins(in_data,
					   static_cast<float>(size_param.u.fs_d.value),
					   marginX,
					   marginY);

		/* Ask for more input than output. Without this the glow would be wrong
		   along every tile boundary, because a pixel near the edge of a tile
		   cannot see the alpha just outside it. */
		req.rect.left -= marginX;
		req.rect.top -= marginY;
		req.rect.right += marginX;
		req.rect.bottom += marginY;

		/* The glow reads RGB behind zero alpha nowhere, but it does need alpha
		   there, and it must not disturb whatever RGB is hiding under it. */
		req.preserve_rgb_of_zero_alpha = TRUE;

		ERR(extra->cb->checkout_layer(in_data->effect_ref,
									  IGFX_INPUT,
									  IGFX_INPUT,
									  &req,
									  in_data->current_time,
									  in_data->time_step,
									  in_data->time_scale,
									  &in_result));
	}

	if (!err) {
		/* The effect recolours existing pixels and never grows the layer, so
		   the output is exactly what was asked for, clipped to the input. */
		PF_LRect resultR = extra->input->output_request.rect;

		resultR.left = MAX(resultR.left, in_result.result_rect.left);
		resultR.top = MAX(resultR.top, in_result.result_rect.top);
		resultR.right = MIN(resultR.right, in_result.result_rect.right);
		resultR.bottom = MIN(resultR.bottom, in_result.result_rect.bottom);

		if (IsEmptyRect(&resultR)) {
			AEFX_CLR_STRUCT(resultR);
		}

		UnionRect(&resultR, &extra->output->result_rect);
		UnionRect(&in_result.max_result_rect, &extra->output->max_result_rect);
	}

	ERR2(PF_CHECKIN_PARAM(in_data, &size_param));

	return err;
}

static PF_Err
SmartRender(
	PF_InData				*in_data,
	PF_OutData				*out_data,
	PF_SmartRenderExtra		*extra)
{
	PF_Err			err = PF_Err_NONE;
	PF_Err			err2 = PF_Err_NONE;
	PF_EffectWorld	*input_worldP = NULL;
	PF_EffectWorld	*output_worldP = NULL;

	PF_ParamDef		checked[IGFX_NUM_PARAMS];
	PF_ParamDef		*paramsP[IGFX_NUM_PARAMS];
	bool			checkedOut[IGFX_NUM_PARAMS];

	for (int i = 0; i < IGFX_NUM_PARAMS; ++i) {
		AEFX_CLR_STRUCT(checked[i]);
		paramsP[i] = &checked[i];
		checkedOut[i] = false;
	}

	/* Index 0 is the input layer, which is checked out as pixels, not as a
	   parameter. Everything from index 1 up is a real control. */
	for (int i = IGFX_INPUT + 1; i < IGFX_NUM_PARAMS && !err; ++i) {
		ERR(PF_CHECKOUT_PARAM(in_data,
							  i,
							  in_data->current_time,
							  in_data->time_step,
							  in_data->time_scale,
							  &checked[i]));
		if (!err) {
			checkedOut[i] = true;
		}
	}

	ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, IGFX_INPUT, &input_worldP));
	ERR(extra->cb->checkout_output(in_data->effect_ref, &output_worldP));

	if (!err && input_worldP && output_worldP) {
		RenderContext ctx;
		ResolveContext(in_data, paramsP, ctx);

		/* origin_x and origin_y give each checked out buffer's position in
		   layer coordinates, which is how the larger input buffer lines up
		   with the output tile. */
		const int offsetX = output_worldP->origin_x - input_worldP->origin_x;
		const int offsetY = output_worldP->origin_y - input_worldP->origin_y;

		err = RenderGlow(in_data,
						 input_worldP,
						 output_worldP,
						 offsetX,
						 offsetY,
						 output_worldP->origin_x,
						 output_worldP->origin_y,
						 input_worldP->origin_x,
						 input_worldP->origin_y,
						 ctx);
	}

	/* Check parameters back in whatever happened. */
	for (int i = IGFX_NUM_PARAMS - 1; i > IGFX_INPUT; --i) {
		if (checkedOut[i]) {
			ERR2(PF_CHECKIN_PARAM(in_data, &checked[i]));
		}
	}

	return err;
}

/* ------------------------------------------------------------------------ */
/* Registration and dispatch                                                */
/* ------------------------------------------------------------------------ */

extern "C" DllExport
PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr	inPtr,
	PF_PluginDataCB2	inPluginDataCallBackPtr,
	SPBasicSuite		*inSPBasicSuitePtr,
	const char			*inHostName,
	const char			*inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;

	result = PF_REGISTER_EFFECT_EXT2(
		inPtr,
		inPluginDataCallBackPtr,
		STR_NAME,
		STR_MATCH_NAME,
		STR_CATEGORY,
		AE_RESERVED_INFO,
		"EffectMain",
		"");

	return result;
}

PF_Err
EffectMain(
	PF_Cmd			cmd,
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output,
	void			*extra)
{
	PF_Err err = PF_Err_NONE;

	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:
				err = About(in_data, out_data, params, output);
				break;
			case PF_Cmd_GLOBAL_SETUP:
				err = GlobalSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_GLOBAL_SETDOWN:
				err = GlobalSetdown(in_data, out_data, params, output);
				break;
			case PF_Cmd_PARAMS_SETUP:
				err = ParamsSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_RENDER:
				err = Render(in_data, out_data, params, output);
				break;
			case PF_Cmd_SMART_PRE_RENDER:
				err = PreRender(in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra));
				break;
			case PF_Cmd_SMART_RENDER:
				err = SmartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra));
				break;
			default:
				break;
		}
	} catch (PF_Err &thrown_err) {
		err = thrown_err;
	} catch (const std::bad_alloc &) {
		err = PF_Err_OUT_OF_MEMORY;
	}

	return err;
}
