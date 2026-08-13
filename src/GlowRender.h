/*
	GlowRender.h

	Glow field generation and blend modes for Inner Glow Effect.

	Copyright (c) 2026 draexon. MIT License, see LICENSE.
	https://github.com/draexon/Inner-Glow-Effect

	Everything here is a pure function of its arguments. No After Effects types,
	no globals, no caches, no static state. That is deliberate:
	PF_OutFlag2_SUPPORTS_THREADED_RENDERING means After Effects may drive the
	render path from several threads and several frames at once, so nothing in
	the render code is allowed to remember anything between calls.
*/

#ifndef GLOWRENDER_H
#define GLOWRENDER_H

#include <cstdint>

namespace igfx {

/*
	Blend modes, in the order After Effects lists them in the Inner Glow layer
	style. The values are 1-based to match what After Effects reports for a
	popup parameter.
*/
enum BlendMode {
	kBlend_NORMAL = 1,
	kBlend_DISSOLVE,
	kBlend_DARKEN,
	kBlend_MULTIPLY,
	kBlend_COLOR_BURN,
	kBlend_LINEAR_BURN,
	kBlend_DARKER_COLOR,
	kBlend_LIGHTEN,
	kBlend_SCREEN,
	kBlend_COLOR_DODGE,
	kBlend_LINEAR_DODGE,
	kBlend_LIGHTER_COLOR,
	kBlend_OVERLAY,
	kBlend_SOFT_LIGHT,
	kBlend_HARD_LIGHT,
	kBlend_VIVID_LIGHT,
	kBlend_LINEAR_LIGHT,
	kBlend_PIN_LIGHT,
	kBlend_HARD_MIX,
	kBlend_DIFFERENCE,
	kBlend_EXCLUSION,
	kBlend_SUBTRACT,
	kBlend_DIVIDE,
	kBlend_HUE,
	kBlend_SATURATION,
	kBlend_COLOR,
	kBlend_LUMINOSITY,

	kBlend_COUNT = kBlend_LUMINOSITY
};

enum Technique {
	kTechnique_SOFTER = 1,
	kTechnique_PRECISE
};

enum Source {
	kSource_CENTER = 1,
	kSource_EDGE
};

struct Rgb {
	float r;
	float g;
	float b;
};

/* Everything BuildGlowField needs, resolved once per render call. */
struct GlowSettings {
	float	sizePx;			// glow reach in buffer pixels, already downsample-corrected
	float	choke;			// 0..1, how much of the reach stays fully solid
	float	range;			// 0.01..1, remaps the falloff curve
	float	noise;			// 0..1
	int		technique;		// Technique
	int		source;			// Source

	// Layer coordinates of buffer pixel (0, 0). The noise dither is keyed to
	// these rather than to buffer position, so the pattern stays pinned to the
	// layer instead of crawling when the render region moves.
	int		originX;
	int		originY;
};

enum ColorType {
	kColorType_SINGLE = 1,
	kColorType_GRADIENT
};

/* The colour side of the composite, resolved once per render call. */
struct CompositeSettings {
	Rgb		colorA;			// the single colour, and the gradient's start
	Rgb		colorB;			// the gradient's end; ignored when not gradient
	float	amount;			// Opacity * Color Opacity, 0..1
	int		blendMode;		// BlendMode
	bool	useGradient;
	float	midpointExponent;	// precomputed, see MidpointExponent()
	float	smoothness;		// 0..1, eases the ramp between the two colours
};

/*
	Converts the Gradient Midpoint control (0..100) into the exponent that
	SampleGradient raises its coordinate to. A midpoint of 50 gives 1.0, which
	leaves the ramp linear.
*/
float MidpointExponent(float midpointPercent);

/*
	Turns an alpha plane into a glow coverage field.

	alphaP    in,  width * height floats, 0..1
	fieldP    out, width * height floats, 0..1, how strongly a pixel glows
	positionP out, width * height floats, 0..1, how far along the falloff a
	               pixel sits, which is what a gradient is painted across.
	               Pass NULL when the glow is a single colour and it is skipped.
	scratch   in,  width * height floats of workspace

	The field is not masked by alpha. Pixels outside the shape are left at
	whatever the falloff says, because the caller re-premultiplies by the
	original alpha, which zeroes them anyway. Masking here as well would fade
	the glow across antialiased edges, which is exactly where it should be
	strongest.
*/
void BuildGlowField(
	const float		*alphaP,
	int				width,
	int				height,
	const GlowSettings &settings,
	float			*fieldP,
	float			*positionP,
	float			*scratch);

/*
	Blends one pixel. base is the incoming pixel, src the glow colour. Both are
	straight (unpremultiplied) colour.
*/
Rgb BlendPixel(int mode, const Rgb &base, const Rgb &src);

/* Picks the glow colour for one pixel: the single colour, or the gradient
   sampled at the given falloff position. */
Rgb SampleGlowColor(const CompositeSettings &settings, float position);

/*
	The whole per-pixel colour pipeline on straight colour: blend the glow
	colour over the base, scaled by the glow coverage and the opacity settings.

	glow is the coverage from fieldP, position the falloff coordinate from
	positionP. position is ignored when the glow is a single colour.

	layerX and layerY are layer-space coordinates, used only so that the
	Dissolve mode's dither pattern stays pinned to the layer instead of
	crawling when the render region moves.

	Alpha is never an argument and is never touched.
*/
Rgb CompositePixel(
	const CompositeSettings &settings,
	const Rgb		&base,
	float			glow,
	float			position,
	int				layerX,
	int				layerY);

/* Deterministic 0..1 hash. Same coordinates always give the same value, on
   every thread and every frame. */
float HashUnit(int x, int y);

}	// namespace igfx

#endif	// GLOWRENDER_H
