/*
	InnerGlowEffect.h

	Inner Glow Effect - After Effects' Inner Glow layer style, as a real effect.

	After Effects offers Inner Glow only as a Layer Style. Layer Styles always
	render after every effect on the layer and cannot be reordered, and they are
	buried in the timeline rather than sitting in Effect Controls. This plug-in
	does the same job as an effect, so it can be placed anywhere in the effect
	chain and composites against whatever alpha exists at that point.
*/

#ifndef INNERGLOWEFFECT_H
#define INNERGLOWEFFECT_H

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_EffectPixelFormat.h"
#include "AE_Macros.h"
#include "Param_Utils.h"

#define MAJOR_VERSION	1
#define MINOR_VERSION	0
#define BUG_VERSION		0
#define STAGE_VERSION	PF_Stage_RELEASE
#define BUILD_VERSION	1

#define STR_NAME			"Inner Glow Effect"
#define STR_MATCH_NAME		"DRAEXON InnerGlowEffect"
#define STR_CATEGORY		"Stylize"
#define STR_DESCRIPTION		"The Inner Glow layer style as a real effect, so it can be placed anywhere in the effect chain."

/*
	Parameter indices.

	Never reorder or remove an entry once a version has shipped: After Effects
	stores parameter values by index, so reordering silently rewires every
	project that already uses the effect. New parameters go on the end only,
	just before IGFX_NUM_PARAMS.
*/
enum {
	IGFX_INPUT = 0,
	IGFX_BLEND_MODE,
	IGFX_OPACITY,
	IGFX_NOISE,
	IGFX_COLOR_TYPE,
	IGFX_COLOR,
	IGFX_COLOR_OPACITY,
	IGFX_END_COLOR,
	IGFX_GRADIENT_MIDPOINT,
	IGFX_GRADIENT_SMOOTHNESS,
	IGFX_TECHNIQUE,
	IGFX_SOURCE,
	IGFX_CHOKE,
	IGFX_SIZE,
	IGFX_RANGE,

	IGFX_NUM_PARAMS
};

/*
	Popup item lists. After Effects separates items with '|'.
	The order here fixes the 1-based values the popups report, which the
	BlendMode, Technique and Source enums in GlowRender.h mirror exactly.
*/
#define IGFX_BLEND_MODE_ITEMS \
	"Normal|Dissolve|Darken|Multiply|Color Burn|Linear Burn|Darker Color|" \
	"Lighten|Screen|Color Dodge|Linear Dodge|Lighter Color|Overlay|" \
	"Soft Light|Hard Light|Vivid Light|Linear Light|Pin Light|Hard Mix|" \
	"Difference|Exclusion|Subtract|Divide|Hue|Saturation|Color|Luminosity"

#define IGFX_BLEND_MODE_COUNT	27
#define IGFX_BLEND_MODE_DFLT	9		/* Screen, matching the layer style */

#define IGFX_TECHNIQUE_ITEMS	"Softer|Precise"
#define IGFX_TECHNIQUE_COUNT	2
#define IGFX_TECHNIQUE_DFLT		1		/* Softer */

#define IGFX_SOURCE_ITEMS		"Center|Edge"
#define IGFX_SOURCE_COUNT		2
#define IGFX_SOURCE_DFLT		2		/* Edge */

#define IGFX_COLOR_TYPE_ITEMS	"Single Color|Gradient"
#define IGFX_COLOR_TYPE_COUNT	2
#define IGFX_COLOR_TYPE_DFLT	1		/* Single Color */

#define IGFX_OPACITY_MIN		0.0
#define IGFX_OPACITY_MAX		100.0
#define IGFX_OPACITY_DFLT		75.0

#define IGFX_NOISE_MIN			0.0
#define IGFX_NOISE_MAX			100.0
#define IGFX_NOISE_DFLT			0.0

#define IGFX_COLOR_OPACITY_MIN	0.0
#define IGFX_COLOR_OPACITY_MAX	100.0
#define IGFX_COLOR_OPACITY_DFLT	100.0

#define IGFX_CHOKE_MIN			0.0
#define IGFX_CHOKE_MAX			100.0
#define IGFX_CHOKE_DFLT			0.0

#define IGFX_SIZE_MIN			0.0
#define IGFX_SIZE_MAX			250.0
#define IGFX_SIZE_DFLT			5.0

#define IGFX_RANGE_MIN			1.0
#define IGFX_RANGE_MAX			100.0
#define IGFX_RANGE_DFLT			50.0

#define IGFX_GRAD_MIDPOINT_MIN	0.0
#define IGFX_GRAD_MIDPOINT_MAX	100.0
#define IGFX_GRAD_MIDPOINT_DFLT	50.0

#define IGFX_GRAD_SMOOTH_MIN	0.0
#define IGFX_GRAD_SMOOTH_MAX	100.0
#define IGFX_GRAD_SMOOTH_DFLT	100.0

/* The layer style's default glow colour. */
#define IGFX_COLOR_RED_DFLT		255
#define IGFX_COLOR_GREEN_DFLT	255
#define IGFX_COLOR_BLUE_DFLT	190

/* Where the gradient lands once the glow has faded out. Warm enough that
   switching Color Type to Gradient shows an obvious difference. */
#define IGFX_END_COLOR_RED_DFLT		255
#define IGFX_END_COLOR_GREEN_DFLT	128
#define IGFX_END_COLOR_BLUE_DFLT	0

#ifdef __cplusplus
extern "C" {
#endif

DllExport PF_Err EffectMain(
	PF_Cmd			cmd,
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output,
	void			*extra);

#ifdef __cplusplus
}
#endif

#endif	// INNERGLOWEFFECT_H
