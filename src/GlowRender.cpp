/*
	GlowRender.cpp

	Glow field generation and blend modes. See GlowRender.h for why nothing
	here holds state.
*/

#include "GlowRender.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace igfx {

namespace {

const float kInfinity = 1.0e20f;

inline float Clamp01(float v)
{
	if (v < 0.0f) {
		return 0.0f;
	}
	if (v > 1.0f) {
		return 1.0f;
	}
	return v;
}

inline float Lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

// ---------------------------------------------------------------------------
// Exact Euclidean distance transform
//
// Felzenszwalb and Huttenlocher's algorithm: two passes of a 1D squared
// distance transform, columns then rows, linear in the number of pixels.
// ---------------------------------------------------------------------------

void SquaredDistance1D(
	const float	*fP,
	float		*dP,
	int			n,
	int			*vP,
	float		*zP)
{
	int k = 0;

	vP[0] = 0;
	zP[0] = -kInfinity;
	zP[1] = kInfinity;

	for (int q = 1; q < n; ++q) {
		float s = ((fP[q] + static_cast<float>(q) * q) -
				   (fP[vP[k]] + static_cast<float>(vP[k]) * vP[k])) /
				  (2.0f * static_cast<float>(q) - 2.0f * static_cast<float>(vP[k]));

		while (k > 0 && s <= zP[k]) {
			--k;
			s = ((fP[q] + static_cast<float>(q) * q) -
				 (fP[vP[k]] + static_cast<float>(vP[k]) * vP[k])) /
				(2.0f * static_cast<float>(q) - 2.0f * static_cast<float>(vP[k]));
		}

		++k;
		vP[k] = q;
		zP[k] = s;
		zP[k + 1] = kInfinity;
	}

	k = 0;
	for (int q = 0; q < n; ++q) {
		while (zP[k + 1] < static_cast<float>(q)) {
			++k;
		}
		const float dist = static_cast<float>(q) - static_cast<float>(vP[k]);
		dP[q] = dist * dist + fP[vP[k]];
	}
}

// Fills distP with the distance, in pixels, from every pixel to the nearest
// pixel whose alpha is below 0.5. Pixels that are themselves outside get 0.
void EuclideanDistanceInside(
	const float	*alphaP,
	int			width,
	int			height,
	float		*distP,
	float		*scratchP)
{
	const int total = width * height;
	const int longest = std::max(width, height);

	for (int i = 0; i < total; ++i) {
		scratchP[i] = (alphaP[i] < 0.5f) ? 0.0f : kInfinity;
	}

	std::vector<float>	lineIn(longest);
	std::vector<float>	lineOut(longest);
	std::vector<int>	hullIndex(longest);
	std::vector<float>	hullBound(longest + 1);

	// Columns.
	for (int x = 0; x < width; ++x) {
		for (int y = 0; y < height; ++y) {
			lineIn[y] = scratchP[y * width + x];
		}
		SquaredDistance1D(lineIn.data(), lineOut.data(), height,
						  hullIndex.data(), hullBound.data());
		for (int y = 0; y < height; ++y) {
			scratchP[y * width + x] = lineOut[y];
		}
	}

	// Rows.
	for (int y = 0; y < height; ++y) {
		float *rowP = scratchP + static_cast<size_t>(y) * width;

		for (int x = 0; x < width; ++x) {
			lineIn[x] = rowP[x];
		}
		SquaredDistance1D(lineIn.data(), lineOut.data(), width,
						  hullIndex.data(), hullBound.data());
		for (int x = 0; x < width; ++x) {
			distP[static_cast<size_t>(y) * width + x] = std::sqrt(lineOut[x]);
		}
	}
}

// ---------------------------------------------------------------------------
// Separable box blur, run three times to approximate a Gaussian
// ---------------------------------------------------------------------------

void BoxBlurHorizontal(const float *srcP, float *dstP, int width, int height, int radius)
{
	const float scale = 1.0f / static_cast<float>(2 * radius + 1);

	for (int y = 0; y < height; ++y) {
		const float	*inRowP = srcP + static_cast<size_t>(y) * width;
		float		*outRowP = dstP + static_cast<size_t>(y) * width;

		// Prime the running sum with the left edge clamped outward.
		float sum = inRowP[0] * static_cast<float>(radius + 1);
		for (int x = 1; x <= radius; ++x) {
			sum += inRowP[std::min(x, width - 1)];
		}

		for (int x = 0; x < width; ++x) {
			outRowP[x] = sum * scale;

			const int addIndex = std::min(x + radius + 1, width - 1);
			const int dropIndex = std::max(x - radius, 0);
			sum += inRowP[addIndex] - inRowP[dropIndex];
		}
	}
}

void BoxBlurVertical(const float *srcP, float *dstP, int width, int height, int radius)
{
	const float scale = 1.0f / static_cast<float>(2 * radius + 1);

	for (int x = 0; x < width; ++x) {
		float sum = srcP[x] * static_cast<float>(radius + 1);
		for (int y = 1; y <= radius; ++y) {
			sum += srcP[static_cast<size_t>(std::min(y, height - 1)) * width + x];
		}

		for (int y = 0; y < height; ++y) {
			dstP[static_cast<size_t>(y) * width + x] = sum * scale;

			const int addIndex = std::min(y + radius + 1, height - 1);
			const int dropIndex = std::max(y - radius, 0);
			sum += srcP[static_cast<size_t>(addIndex) * width + x] -
				   srcP[static_cast<size_t>(dropIndex) * width + x];
		}
	}
}

void BlurField(float *fieldP, int width, int height, float sigma, float *scratchP)
{
	if (sigma < 0.5f) {
		return;
	}

	/*
		Three box passes whose combined variance matches the requested Gaussian.

		A box of width w has variance (w*w - 1) / 12, so three of them give
		3 * (w*w - 1) / 12, and setting that equal to sigma squared gives
		w = sqrt(1 + 4 * sigma * sigma).

		Getting this wrong is not subtle: the passes reach 3 * radius pixels, so
		an overlarge radius spreads the result far past the requested sigma.
	*/
	const float width_f = std::sqrt(1.0f + 4.0f * sigma * sigma);
	int radius = static_cast<int>(std::floor((width_f - 1.0f) * 0.5f + 0.5f));
	if (radius < 1) {
		radius = 1;
	}
	const int maxRadius = std::max(1, std::max(width, height) - 1);
	if (radius > maxRadius) {
		radius = maxRadius;
	}

	for (int pass = 0; pass < 3; ++pass) {
		BoxBlurHorizontal(fieldP, scratchP, width, height, radius);
		BoxBlurVertical(scratchP, fieldP, width, height, radius);
	}
}

// ---------------------------------------------------------------------------
// Blend mode helpers
// ---------------------------------------------------------------------------

inline float Luminosity(const Rgb &c)
{
	return 0.30f * c.r + 0.59f * c.g + 0.11f * c.b;
}

Rgb ClipColor(Rgb c)
{
	const float lum = Luminosity(c);
	const float lo = std::min(c.r, std::min(c.g, c.b));
	const float hi = std::max(c.r, std::max(c.g, c.b));

	if (lo < 0.0f) {
		const float d = lum - lo;
		if (d > 1.0e-6f) {
			c.r = lum + (c.r - lum) * lum / d;
			c.g = lum + (c.g - lum) * lum / d;
			c.b = lum + (c.b - lum) * lum / d;
		}
	}

	if (hi > 1.0f) {
		const float d = hi - lum;
		if (d > 1.0e-6f) {
			c.r = lum + (c.r - lum) * (1.0f - lum) / d;
			c.g = lum + (c.g - lum) * (1.0f - lum) / d;
			c.b = lum + (c.b - lum) * (1.0f - lum) / d;
		}
	}

	return c;
}

Rgb SetLuminosity(const Rgb &c, float lum)
{
	const float d = lum - Luminosity(c);
	Rgb out;
	out.r = c.r + d;
	out.g = c.g + d;
	out.b = c.b + d;
	return ClipColor(out);
}

inline float Saturation(const Rgb &c)
{
	return std::max(c.r, std::max(c.g, c.b)) - std::min(c.r, std::min(c.g, c.b));
}

Rgb SetSaturation(const Rgb &c, float sat)
{
	// Rank the three channels, stretch mid and max, flatten min.
	float *chan[3];
	Rgb out = c;
	chan[0] = &out.r;
	chan[1] = &out.g;
	chan[2] = &out.b;

	// Simple sort of three pointers by the value they point at.
	for (int i = 0; i < 2; ++i) {
		for (int j = i + 1; j < 3; ++j) {
			if (*chan[j] < *chan[i]) {
				float *tmp = chan[i];
				chan[i] = chan[j];
				chan[j] = tmp;
			}
		}
	}

	const float lo = *chan[0];
	const float mid = *chan[1];
	const float hi = *chan[2];

	if (hi > lo) {
		*chan[1] = (mid - lo) * sat / (hi - lo);
		*chan[2] = sat;
	} else {
		*chan[1] = 0.0f;
		*chan[2] = 0.0f;
	}
	*chan[0] = 0.0f;

	return out;
}

inline float ChannelColorBurn(float base, float src)
{
	if (src <= 0.0f) {
		return 0.0f;
	}
	return 1.0f - std::min(1.0f, (1.0f - base) / src);
}

inline float ChannelColorDodge(float base, float src)
{
	if (src >= 1.0f) {
		return 1.0f;
	}
	return std::min(1.0f, base / (1.0f - src));
}

inline float ChannelHardLight(float base, float src)
{
	if (src <= 0.5f) {
		return 2.0f * base * src;
	}
	return 1.0f - 2.0f * (1.0f - base) * (1.0f - src);
}

inline float ChannelSoftLight(float base, float src)
{
	if (src <= 0.5f) {
		return base - (1.0f - 2.0f * src) * base * (1.0f - base);
	}

	float d;
	if (base <= 0.25f) {
		d = ((16.0f * base - 12.0f) * base + 4.0f) * base;
	} else {
		d = std::sqrt(base < 0.0f ? 0.0f : base);
	}
	return base + (2.0f * src - 1.0f) * (d - base);
}

inline float ChannelVividLight(float base, float src)
{
	if (src <= 0.5f) {
		return ChannelColorBurn(base, 2.0f * src);
	}
	return ChannelColorDodge(base, 2.0f * (src - 0.5f));
}

inline float ChannelPinLight(float base, float src)
{
	if (src <= 0.5f) {
		return std::min(base, 2.0f * src);
	}
	return std::max(base, 2.0f * src - 1.0f);
}

inline float ChannelDivide(float base, float src)
{
	if (src <= 0.0f) {
		return base > 0.0f ? 1.0f : 0.0f;
	}
	return std::min(1.0f, base / src);
}

// Applies a per-channel function across a pixel.
template <typename Fn>
inline Rgb PerChannel(const Rgb &base, const Rgb &src, Fn fn)
{
	Rgb out;
	out.r = fn(base.r, src.r);
	out.g = fn(base.g, src.g);
	out.b = fn(base.b, src.b);
	return out;
}

}	// namespace

// ---------------------------------------------------------------------------

float HashUnit(int x, int y)
{
	uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^
				 static_cast<uint32_t>(y) * 0xD8163841u;

	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	h *= 0x297A2D39u;
	h ^= h >> 15;

	return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f;
}

void BuildGlowField(
	const float			*alphaP,
	int					width,
	int					height,
	const GlowSettings	&settings,
	float				*fieldP,
	float				*scratch)
{
	const int total = width * height;

	if (total <= 0) {
		return;
	}

	// A glow with no reach contributes nothing, whichever way it is sourced.
	if (settings.sizePx < 0.5f) {
		for (int i = 0; i < total; ++i) {
			fieldP[i] = 0.0f;
		}
		return;
	}

	const float solidTo = settings.choke * settings.sizePx;
	const float fadeSpan = settings.sizePx - solidTo;

	/*
		Both techniques produce the same thing: how strongly a pixel glows when
		the glow is sourced from the edge. They differ in how they measure the
		distance to that edge.

		Precise walks an exact distance transform, so the falloff follows the
		shape's corners tightly and a pixel at a given depth glows the same
		whether it sits behind a flat edge or a corner.

		Softer works off a blurred alpha instead. Blurring accumulates
		transparency from every direction at once, so inside corners come out
		brighter and the whole falloff rounds off. It also cannot leak, because
		alpha outside the shape is zero: there is no out-of-shape value for the
		blur to drag inward.
	*/
	if (settings.technique == kTechnique_PRECISE) {

		// The distance transform writes into fieldP and uses scratch as
		// workspace. Once it returns, scratch is free again.
		EuclideanDistanceInside(alphaP, width, height, fieldP, scratch);

		for (int i = 0; i < total; ++i) {
			// The outermost opaque pixel sits one pixel from the nearest
			// transparent one. Subtracting that puts it exactly on the edge,
			// where it should read as full glow rather than one step down.
			float dist = fieldP[i] - 1.0f;
			if (dist < 0.0f) {
				dist = 0.0f;
			}

			float ramp;
			if (fadeSpan <= 1.0e-6f) {
				ramp = (dist <= solidTo) ? 0.0f : 1.0f;
			} else {
				ramp = Clamp01((dist - solidTo) / fadeSpan);
			}

			fieldP[i] = 1.0f - ramp;
		}

	} else {

		for (int i = 0; i < total; ++i) {
			fieldP[i] = alphaP[i];
		}

		/*
			A blurred step edge reads 0.5 on the edge and climbs to 1.0 by about
			three sigma inside it, so a third of the reach as sigma puts the
			glow back at zero right around Size pixels in.
		*/
		BlurField(fieldP, width, height, settings.sizePx / 3.0f, scratch);

		/*
			Blurred alpha reads 0.5 on the geometric edge and climbs to 1.0
			inside. The geometric edge sits half a pixel outside the centre of
			the outermost opaque pixel, so that pixel reads a little above 0.5,
			not 0.5 exactly.

			Normalising by that value is what keeps Softer and Precise agreeing
			at the edge. Precise makes the same half-pixel correction by
			subtracting one from its distance transform; without the equivalent
			here, flipping Technique would visibly change edge brightness.
		*/
		const float sigma = settings.sizePx / 3.0f;
		const float edgeValue = 0.5f + 0.5f * std::erf(0.5f / (sigma * 1.41421356f));

		float normalise = 1.0f / (1.0f - edgeValue);
		if (normalise > 8.0f) {
			normalise = 8.0f;
		}

		float chokeSpan = 1.0f - settings.choke;
		if (chokeSpan < 1.0e-4f) {
			chokeSpan = 1.0e-4f;
		}

		const float gain = normalise / chokeSpan;

		for (int i = 0; i < total; ++i) {
			fieldP[i] = Clamp01((1.0f - fieldP[i]) * gain);
		}
	}

	// Range remaps the falloff curve. 50% is neutral, higher spreads the glow
	// further in, lower tightens it against the edge.
	float range = settings.range;
	if (range < 0.01f) {
		range = 0.01f;
	}
	const float rangeExponent = 0.5f / range;
	const bool  applyRange = std::fabs(rangeExponent - 1.0f) > 1.0e-4f;

	const bool invert = (settings.source == kSource_CENTER);

	if (invert || applyRange) {
		for (int i = 0; i < total; ++i) {
			// Center is simply the complement of Edge: brightest deep inside
			// the shape, gone by the time it reaches the alpha edge.
			float glow = invert ? (1.0f - fieldP[i]) : fieldP[i];

			if (applyRange && glow > 0.0f && glow < 1.0f) {
				glow = std::pow(glow, rangeExponent);
			}

			fieldP[i] = glow;
		}
	}

	// Noise last, so the blur above cannot smooth it away.
	if (settings.noise > 0.0f) {
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const size_t index = static_cast<size_t>(y) * width + x;
				const float dither = HashUnit(settings.originX + x, settings.originY + y);
				fieldP[index] *= 1.0f - settings.noise * dither;
			}
		}
	}
}

Rgb BlendPixel(int mode, const Rgb &base, const Rgb &src)
{
	switch (mode) {

		case kBlend_DARKEN:
			return PerChannel(base, src, [](float b, float s) { return std::min(b, s); });

		case kBlend_MULTIPLY:
			return PerChannel(base, src, [](float b, float s) { return b * s; });

		case kBlend_COLOR_BURN:
			return PerChannel(base, src, ChannelColorBurn);

		case kBlend_LINEAR_BURN:
			return PerChannel(base, src, [](float b, float s) { return b + s - 1.0f; });

		case kBlend_DARKER_COLOR:
			return (Luminosity(base) <= Luminosity(src)) ? base : src;

		case kBlend_LIGHTEN:
			return PerChannel(base, src, [](float b, float s) { return std::max(b, s); });

		case kBlend_SCREEN:
			return PerChannel(base, src, [](float b, float s) {
				return 1.0f - (1.0f - b) * (1.0f - s);
			});

		case kBlend_COLOR_DODGE:
			return PerChannel(base, src, ChannelColorDodge);

		case kBlend_LINEAR_DODGE:
			return PerChannel(base, src, [](float b, float s) { return b + s; });

		case kBlend_LIGHTER_COLOR:
			return (Luminosity(base) > Luminosity(src)) ? base : src;

		// Overlay is Hard Light with the two inputs swapped.
		case kBlend_OVERLAY:
			return PerChannel(base, src, [](float b, float s) {
				return ChannelHardLight(s, b);
			});

		case kBlend_SOFT_LIGHT:
			return PerChannel(base, src, ChannelSoftLight);

		case kBlend_HARD_LIGHT:
			return PerChannel(base, src, ChannelHardLight);

		case kBlend_VIVID_LIGHT:
			return PerChannel(base, src, ChannelVividLight);

		case kBlend_LINEAR_LIGHT:
			return PerChannel(base, src, [](float b, float s) {
				return b + 2.0f * s - 1.0f;
			});

		case kBlend_PIN_LIGHT:
			return PerChannel(base, src, ChannelPinLight);

		case kBlend_HARD_MIX:
			return PerChannel(base, src, [](float b, float s) {
				return (ChannelVividLight(b, s) < 0.5f) ? 0.0f : 1.0f;
			});

		case kBlend_DIFFERENCE:
			return PerChannel(base, src, [](float b, float s) {
				return std::fabs(b - s);
			});

		case kBlend_EXCLUSION:
			return PerChannel(base, src, [](float b, float s) {
				return b + s - 2.0f * b * s;
			});

		case kBlend_SUBTRACT:
			return PerChannel(base, src, [](float b, float s) { return b - s; });

		case kBlend_DIVIDE:
			return PerChannel(base, src, ChannelDivide);

		case kBlend_HUE:
			return SetLuminosity(SetSaturation(src, Saturation(base)), Luminosity(base));

		case kBlend_SATURATION:
			return SetLuminosity(SetSaturation(base, Saturation(src)), Luminosity(base));

		case kBlend_COLOR:
			return SetLuminosity(src, Luminosity(base));

		case kBlend_LUMINOSITY:
			return SetLuminosity(base, Luminosity(src));

		// Dissolve picks whole pixels rather than mixing them, so it is resolved
		// in CompositePixel where the coverage and the coordinates are known.
		case kBlend_DISSOLVE:
		case kBlend_NORMAL:
		default:
			return src;
	}
}

Rgb CompositePixel(
	const CompositeSettings	&settings,
	const Rgb				&base,
	float					glow,
	int						layerX,
	int						layerY)
{
	float coverage = glow * settings.amount;

	if (coverage <= 0.0f) {
		return base;
	}
	if (coverage > 1.0f) {
		coverage = 1.0f;
	}

	if (settings.blendMode == kBlend_DISSOLVE) {
		// All or nothing per pixel, dithered against a hash pinned to the layer
		// so the pattern does not crawl when the render region moves.
		return (HashUnit(layerX, layerY) < coverage) ? settings.color : base;
	}

	const Rgb blended = BlendPixel(settings.blendMode, base, settings.color);

	Rgb out;
	out.r = Lerp(base.r, blended.r, coverage);
	out.g = Lerp(base.g, blended.g, coverage);
	out.b = Lerp(base.b, blended.b, coverage);
	return out;
}

}	// namespace igfx
