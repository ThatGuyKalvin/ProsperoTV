/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_osd.h"

#include "iptv_osd_font.h"
#include "iptv_overlay_blend.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* A colour as BT.709 limited-range YUV with 10-bit precision (8-bit frames drop two bits). */
typedef struct osd_colour
{
    int y;
    int u;
    int v;
} osd_colour_t;

static osd_colour_t osd_rgb(unsigned r, unsigned g, unsigned b)
{
    const float red = (float)r / 255.0f;
    const float green = (float)g / 255.0f;
    const float blue = (float)b / 255.0f;
    const float luma = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
    const osd_colour_t colour = {
        (int)(64.0f + 876.0f * luma + 0.5f),
        (int)(512.0f + 896.0f * (blue - luma) / 1.8556f + 0.5f),
        (int)(512.0f + 896.0f * (red - luma) / 1.5748f + 0.5f),
    };
    return colour;
}

typedef struct osd_canvas
{
    const iptv_osd_surface_t *surface;
    float scale;       /* area width / 1920 */
    osd_colour_t base; /* the backdrop colour */
    size_t chroma;     /* first chroma sample */
    size_t ox;         /* where the area starts on the surface */
    size_t oy;
} osd_canvas_t;

static int px(const osd_canvas_t *canvas, float reference)
{
    return (int)(reference * canvas->scale + 0.5f);
}

static float pxf(const osd_canvas_t *canvas, float reference)
{
    return reference * canvas->scale;
}

/* Puts `colour` over one luma sample with `alpha` out of 256. */
static void blend_luma(const osd_canvas_t *canvas, int x, int y, osd_colour_t colour, int alpha)
{
    const iptv_osd_surface_t *surface = canvas->surface;
    if (alpha <= 0 || x < 0 || y < 0 || x >= (int)surface->width || y >= (int)surface->height)
        return;
    const size_t index = ((size_t)y + canvas->oy) * surface->pitch + (size_t)x + canvas->ox;
    iptv_overlay_luma(surface, index, colour.y, alpha);
}

static void blend_chroma(const osd_canvas_t *canvas, int cx, int cy, osd_colour_t colour, int alpha)
{
    const iptv_osd_surface_t *surface = canvas->surface;
    if (alpha <= 0 || cx < 0 || cy < 0 || cx * 2 >= (int)surface->width ||
        cy * 2 >= (int)surface->height)
        return;
    const size_t index = canvas->chroma + ((size_t)cy + canvas->oy / 2u) * surface->pitch +
                         2u * ((size_t)cx + canvas->ox / 2u);
    iptv_overlay_chroma(surface, index, colour.u, colour.v, alpha);
}

/* ---- Anti-aliased shapes, from signed distances (negative inside) ---- */

typedef struct osd_shape
{
    int kind;             /* 0 rounded rectangle, 1 circle, 2 ring, 3 capsule (line segment) */
    float x0, y0, x1, y1; /* rectangle: corners; circle/ring: centre in x0,y0; capsule: ends */
    float radius;         /* corner, circle or capsule radius */
    float width;          /* ring thickness */
} osd_shape_t;

static float clampf(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static float shape_distance(const osd_shape_t *shape, float x, float y)
{
    switch (shape->kind)
    {
    case 0:
    {
        const float cx = (shape->x0 + shape->x1) * 0.5f;
        const float cy = (shape->y0 + shape->y1) * 0.5f;
        const float qx = fabsf(x - cx) - ((shape->x1 - shape->x0) * 0.5f - shape->radius);
        const float qy = fabsf(y - cy) - ((shape->y1 - shape->y0) * 0.5f - shape->radius);
        const float ox = qx > 0.0f ? qx : 0.0f;
        const float oy = qy > 0.0f ? qy : 0.0f;
        const float inside = qx > qy ? (qx < 0.0f ? qx : 0.0f) : (qy < 0.0f ? qy : 0.0f);
        return sqrtf(ox * ox + oy * oy) + inside - shape->radius;
    }
    case 1:
        return hypotf(x - shape->x0, y - shape->y0) - shape->radius;
    case 2:
        return fabsf(hypotf(x - shape->x0, y - shape->y0) - shape->radius) - shape->width * 0.5f;
    default:
    {
        const float dx = shape->x1 - shape->x0;
        const float dy = shape->y1 - shape->y0;
        const float length = dx * dx + dy * dy;
        const float t = length > 0.0f
                            ? clampf(((x - shape->x0) * dx + (y - shape->y0) * dy) / length, 0, 1)
                            : 0.0f;
        return hypotf(x - shape->x0 - dx * t, y - shape->y0 - dy * t) - shape->radius;
    }
    }
}

static void shape_bounds(const osd_shape_t *shape, int *x0, int *y0, int *x1, int *y1)
{
    float left = shape->x0, top = shape->y0, right = shape->x1, bottom = shape->y1;
    float grow = 2.0f;
    if (shape->kind == 1 || shape->kind == 2)
    {
        grow += shape->radius + shape->width;
        right = left;
        bottom = top;
    }
    else if (shape->kind == 3)
    {
        grow += shape->radius;
        if (right < left)
        {
            const float swap = left;
            left = right;
            right = swap;
        }
        if (bottom < top)
        {
            const float swap = top;
            top = bottom;
            bottom = swap;
        }
    }
    *x0 = (int)floorf(left - grow);
    *y0 = (int)floorf(top - grow);
    *x1 = (int)ceilf(right + grow);
    *y1 = (int)ceilf(bottom + grow);
}

static void fill_shape(const osd_canvas_t *canvas, const osd_shape_t *shape, osd_colour_t colour,
                       float opacity)
{
    int x0, y0, x1, y1;
    shape_bounds(shape, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const float coverage =
                clampf(0.5f - shape_distance(shape, (float)x + 0.5f, (float)y + 0.5f), 0, 1);
            blend_luma(canvas, x, y, colour, (int)(coverage * opacity * 256.0f + 0.5f));
        }
    for (int cy = y0 / 2; cy <= y1 / 2; ++cy)
        for (int cx = x0 / 2; cx <= x1 / 2; ++cx)
        {
            /* A chroma sample covers 2x2 pixels: soften its edge over that width. */
            const float distance =
                shape_distance(shape, (float)cx * 2.0f + 1.0f, (float)cy * 2.0f + 1.0f);
            const float coverage = clampf(0.5f - distance * 0.5f, 0, 1);
            blend_chroma(canvas, cx, cy, colour, (int)(coverage * opacity * 256.0f + 0.5f));
        }
}

static void rounded_rect(const osd_canvas_t *canvas, float x0, float y0, float x1, float y1,
                         float radius, osd_colour_t colour, float opacity)
{
    if (x1 - x0 < 1.0f || y1 - y0 < 1.0f)
        return;
    const float limit = fminf(x1 - x0, y1 - y0) * 0.5f;
    const osd_shape_t shape = {0, x0, y0, x1, y1, radius < limit ? radius : limit, 0};
    fill_shape(canvas, &shape, colour, opacity);
}

static void circle(const osd_canvas_t *canvas, float x, float y, float radius, osd_colour_t colour,
                   float opacity)
{
    const osd_shape_t shape = {1, x, y, x, y, radius, 0};
    fill_shape(canvas, &shape, colour, opacity);
}

static void ring(const osd_canvas_t *canvas, float x, float y, float radius, float width,
                 osd_colour_t colour)
{
    const osd_shape_t shape = {2, x, y, x, y, radius, width};
    fill_shape(canvas, &shape, colour, 1.0f);
}

static void line(const osd_canvas_t *canvas, float x0, float y0, float x1, float y1, float width,
                 osd_colour_t colour)
{
    const osd_shape_t shape = {3, x0, y0, x1, y1, width * 0.5f, 0};
    fill_shape(canvas, &shape, colour, 1.0f);
}

/* ---- The gradient behind the controls ---- */

static void draw_backdrop(osd_canvas_t *canvas, int top)
{
    const iptv_osd_surface_t *surface = canvas->surface;
    const int bottom = (int)surface->height;
    const int width = (int)surface->width;
    const osd_colour_t base = canvas->base;
    for (int y = top; y < bottom; ++y)
    {
        const float t = (float)(y - top) / (float)(bottom - top);
        const float ease = 1.0f - powf(1.0f - t, 2.2f);
        const int alpha = (int)(0.9f * ease * 256.0f);
        const size_t row = ((size_t)y + canvas->oy) * surface->pitch + canvas->ox;
        for (int x = 0; x < width; ++x)
            iptv_overlay_luma(surface, row + (size_t)x, base.y, alpha);
        if (y & 1)
            continue;
        const size_t chroma =
            canvas->chroma + ((size_t)(y / 2) + canvas->oy / 2u) * surface->pitch + canvas->ox;
        for (int x = 0; x < width; x += 2)
            iptv_overlay_chroma(surface, chroma + (size_t)x, base.u, base.v, alpha);
    }
}

/* ---- Text ---- */

typedef struct osd_text_style
{
    const iptv_osd_font_t *font;
    int scale;
} osd_text_style_t;

/* The embedded font and integer scale whose size is closest to `wanted` pixels. */
static osd_text_style_t text_style(int wanted)
{
    osd_text_style_t best = {iptv_osd_fonts[0], 1};
    int best_error = 1 << 30;
    for (size_t index = 0; index < sizeof(iptv_osd_fonts) / sizeof(iptv_osd_fonts[0]); ++index)
        for (int scale = 1; scale <= 3; ++scale)
        {
            const int size = (int)iptv_osd_fonts[index]->size * scale;
            const int error = size > wanted ? size - wanted : wanted - size;
            if (error < best_error)
            {
                best_error = error;
                best.font = iptv_osd_fonts[index];
                best.scale = scale;
            }
        }
    return best;
}

static const iptv_osd_glyph_t *glyph_for(const iptv_osd_font_t *font, char character)
{
    const unsigned code = (unsigned char)character;
    return &font->glyphs[code >= 32u && code <= 126u ? code - 32u : '?' - 32u];
}

static int text_width(osd_text_style_t style, const char *text, size_t length)
{
    int width = 0;
    for (size_t index = 0; index < length && text[index]; ++index)
        width += glyph_for(style.font, text[index])->advance * style.scale;
    return width;
}

static int full_width(osd_text_style_t style, const char *text)
{
    return text ? text_width(style, text, strlen(text)) : 0;
}

static size_t fit_text(osd_text_style_t style, const char *text, int max_width, int *ellipsis)
{
    const size_t length = strlen(text);
    *ellipsis = 0;
    if (text_width(style, text, length) <= max_width)
        return length;
    const int dots = text_width(style, "...", 3);
    size_t fitted = 0;
    int width = 0;
    while (fitted < length)
    {
        const int advance = glyph_for(style.font, text[fitted])->advance * style.scale;
        if (width + advance + dots > max_width)
            break;
        width += advance;
        ++fitted;
    }
    while (fitted && text[fitted - 1u] == ' ')
        --fitted;
    *ellipsis = 1;
    return fitted;
}

static int glyph_alpha(const iptv_osd_font_t *font, const iptv_osd_glyph_t *glyph, int scale,
                       int column, int row)
{
    if (column < 0 || row < 0 || column >= glyph->width * scale || row >= glyph->height * scale)
        return 0;
    return font->bitmap[glyph->offset + (size_t)(row / scale) * glyph->width + column / scale];
}

static int draw_glyphs(const osd_canvas_t *canvas, osd_text_style_t style, int x, int top,
                       const char *text, size_t length, osd_colour_t colour)
{
    for (size_t index = 0; index < length && text[index]; ++index)
    {
        const iptv_osd_glyph_t *glyph = glyph_for(style.font, text[index]);
        const int left = x + glyph->x_offset * style.scale;
        const int upper = top + glyph->y_offset * style.scale;
        const int width = glyph->width * style.scale;
        const int height = glyph->height * style.scale;
        for (int row = 0; row < height; ++row)
            for (int column = 0; column < width; ++column)
            {
                const int alpha = glyph_alpha(style.font, glyph, style.scale, column, row);
                blend_luma(canvas, left + column, upper + row, colour, alpha + (alpha >> 7));
            }
        /* Colour follows the average coverage of each 2x2 block. */
        for (int row = upper & ~1; row < upper + height; row += 2)
            for (int column = left & ~1; column < left + width; column += 2)
            {
                int sum = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx)
                        sum += glyph_alpha(style.font, glyph, style.scale, column + dx - left,
                                           row + dy - upper);
                blend_chroma(canvas, column / 2, row / 2, colour, sum / 4);
            }
        x += glyph->advance * style.scale;
    }
    return x;
}

typedef enum
{
    ALIGN_LEFT,
    ALIGN_RIGHT,
    ALIGN_CENTRE,
} osd_align_t;

/* Draws text centred vertically on `middle`; returns its width. */
static int draw_text(const osd_canvas_t *canvas, osd_text_style_t style, int x, int middle,
                     int max_width, osd_align_t align, const char *text, osd_colour_t colour)
{
    if (!text || !*text || max_width <= 0)
        return 0;
    int ellipsis = 0;
    const size_t length = fit_text(style, text, max_width, &ellipsis);
    const int width =
        text_width(style, text, length) + (ellipsis ? text_width(style, "...", 3) : 0);
    if (align == ALIGN_RIGHT)
        x -= width;
    else if (align == ALIGN_CENTRE)
        x -= width / 2;
    /* Centre the capital height (base minus the top of a capital) on `middle`. */
    const iptv_osd_glyph_t *capital = glyph_for(style.font, 'H');
    const int cap_top = capital->y_offset * style.scale;
    const int cap_height = capital->height * style.scale;
    const int top = middle - cap_top - cap_height / 2;
    int end = draw_glyphs(canvas, style, x, top, text, length, colour);
    if (ellipsis)
        end = draw_glyphs(canvas, style, end, top, "...", 3, colour);
    return width;
}

/* ---- Palette ---- */

typedef struct osd_palette
{
    osd_colour_t white, soft, dim, accent, dark, red, track;
    osd_colour_t cross, circle, square, triangle;
} osd_palette_t;

static osd_palette_t palette(void)
{
    const osd_palette_t colours = {
        osd_rgb(0xff, 0xff, 0xff), osd_rgb(0xe6, 0xe0, 0xea), osd_rgb(0xa9, 0xa0, 0xb2),
        osd_rgb(0xf3, 0xa4, 0x47), osd_rgb(0x12, 0x0e, 0x18), osd_rgb(0xe5, 0x48, 0x4d),
        osd_rgb(0xff, 0xff, 0xff), osd_rgb(0x8a, 0xb4, 0xf8), osd_rgb(0xff, 0x6f, 0x6f),
        osd_rgb(0xf0, 0x9c, 0xe0), osd_rgb(0x5c, 0xd6, 0xb4),
    };
    return colours;
}

/* ---- Button hints: PlayStation symbols on dark discs ---- */

enum
{
    ICON_CROSS,
    ICON_CIRCLE,
    ICON_SQUARE,
    ICON_TRIANGLE,
    ICON_LEFT_RIGHT,
    ICON_UP_DOWN,
};

static void draw_icon(const osd_canvas_t *canvas, int icon, float x, float y, float size)
{
    const osd_palette_t colours = palette();
    const float r = size * 0.5f;
    const float stroke = fmaxf(1.5f, size * 0.09f);
    circle(canvas, x, y, r, osd_rgb(0x2a, 0x24, 0x33), 0.92f);
    const float s = r * 0.42f;
    switch (icon)
    {
    case ICON_CROSS:
        line(canvas, x - s, y - s, x + s, y + s, stroke, colours.cross);
        line(canvas, x - s, y + s, x + s, y - s, stroke, colours.cross);
        break;
    case ICON_CIRCLE:
        ring(canvas, x, y, s * 1.1f, stroke, colours.circle);
        break;
    case ICON_SQUARE:
        line(canvas, x - s, y - s, x + s, y - s, stroke, colours.square);
        line(canvas, x + s, y - s, x + s, y + s, stroke, colours.square);
        line(canvas, x + s, y + s, x - s, y + s, stroke, colours.square);
        line(canvas, x - s, y + s, x - s, y - s, stroke, colours.square);
        break;
    case ICON_TRIANGLE:
    {
        const float h = s * 1.15f;
        line(canvas, x, y - h, x + h, y + h * 0.75f, stroke, colours.triangle);
        line(canvas, x + h, y + h * 0.75f, x - h, y + h * 0.75f, stroke, colours.triangle);
        line(canvas, x - h, y + h * 0.75f, x, y - h, stroke, colours.triangle);
        break;
    }
    case ICON_LEFT_RIGHT:
        line(canvas, x - s * 0.3f, y - s * 0.7f, x - s * 1.0f, y, stroke, colours.white);
        line(canvas, x - s * 1.0f, y, x - s * 0.3f, y + s * 0.7f, stroke, colours.white);
        line(canvas, x + s * 0.3f, y - s * 0.7f, x + s * 1.0f, y, stroke, colours.white);
        line(canvas, x + s * 1.0f, y, x + s * 0.3f, y + s * 0.7f, stroke, colours.white);
        break;
    default:
        line(canvas, x - s * 0.7f, y - s * 0.3f, x, y - s * 1.0f, stroke, colours.white);
        line(canvas, x, y - s * 1.0f, x + s * 0.7f, y - s * 0.3f, stroke, colours.white);
        line(canvas, x - s * 0.7f, y + s * 0.3f, x, y + s * 1.0f, stroke, colours.white);
        line(canvas, x, y + s * 1.0f, x + s * 0.7f, y + s * 0.3f, stroke, colours.white);
        break;
    }
}

static int draw_hint(const osd_canvas_t *canvas, int x, int middle, int icon, const char *label)
{
    const osd_palette_t colours = palette();
    const osd_text_style_t style = text_style(px(canvas, 22.0f));
    const float size = pxf(canvas, 34.0f);
    draw_icon(canvas, icon, (float)x + size * 0.5f, (float)middle, size);
    const int label_x = x + (int)size + px(canvas, 12.0f);
    const int width =
        draw_text(canvas, style, label_x, middle, 1 << 20, ALIGN_LEFT, label, colours.soft);
    return label_x + width + px(canvas, 40.0f);
}

/* ---- Layouts (in 1080p units, scaled by the frame width) ---- */

static int backdrop_top(const osd_canvas_t *canvas)
{
    const int top = (int)canvas->surface->height - px(canvas, 420.0f);
    return (top < 0 ? 0 : top) & ~1;
}

static void draw_timeline(const osd_canvas_t *canvas, int left, int right, int middle,
                          int64_t position, int64_t duration, int seeking, int show_knob)
{
    const osd_palette_t colours = palette();
    const float thickness = pxf(canvas, 8.0f);
    const float top = (float)middle - thickness * 0.5f;
    rounded_rect(canvas, (float)left, top, (float)right, top + thickness, thickness * 0.5f,
                 colours.track, 0.28f);
    if (duration <= 0 || position < 0)
        return;
    const int64_t done = position > duration ? duration : position;
    const float end = (float)left + (float)(right - left) * (float)done / (float)duration;
    rounded_rect(canvas, (float)left, top, end > left + thickness ? end : left + thickness,
                 top + thickness, thickness * 0.5f, colours.accent, 1.0f);
    if (!show_knob)
        return;
    const float knob = pxf(canvas, seeking ? 15.0f : 12.0f);
    circle(canvas, end, (float)middle, knob, colours.white, 1.0f);
    if (seeking)
    {
        /* The target time in a bubble above the knob. */
        char time[24];
        iptv_osd_format_time(position, time, sizeof(time));
        const osd_text_style_t style = text_style(px(canvas, 28.0f));
        const float width = (float)full_width(style, time) + pxf(canvas, 40.0f);
        const float height = pxf(canvas, 50.0f);
        const float bottom = (float)middle - pxf(canvas, 28.0f);
        float bubble_left = end - width * 0.5f;
        bubble_left = clampf(bubble_left, (float)left, (float)right - width);
        rounded_rect(canvas, bubble_left, bottom - height, bubble_left + width, bottom,
                     height * 0.5f, colours.white, 0.96f);
        line(canvas, end - pxf(canvas, 7.0f), bottom - 1.0f, end, bottom + pxf(canvas, 8.0f),
             pxf(canvas, 4.0f), colours.white);
        line(canvas, end + pxf(canvas, 7.0f), bottom - 1.0f, end, bottom + pxf(canvas, 8.0f),
             pxf(canvas, 4.0f), colours.white);
        draw_text(canvas, style, (int)(bubble_left + width * 0.5f), (int)(bottom - height * 0.5f),
                  (int)width, ALIGN_CENTRE, time, colours.dark);
    }
}

static int draw_media(osd_canvas_t *canvas, const iptv_osd_state_t *state)
{
    const iptv_osd_surface_t *surface = canvas->surface;
    const osd_palette_t colours = palette();
    const int width = (int)surface->width;
    const int height = (int)surface->height;
    const int margin = px(canvas, 96.0f);
    const int top = backdrop_top(canvas);
    draw_backdrop(canvas, top);

    if (state->paused)
    {
        /* A large pause badge in the middle of the picture. */
        const float cx = (float)width * 0.5f;
        const float cy = (float)height * 0.42f;
        const float r = pxf(canvas, 70.0f);
        circle(canvas, cx, cy, r, colours.dark, 0.62f);
        ring(canvas, cx, cy, r - pxf(canvas, 1.5f), pxf(canvas, 3.0f), colours.white);
        const float bar = pxf(canvas, 15.0f);
        const float tall = pxf(canvas, 54.0f);
        const float gap = pxf(canvas, 13.0f);
        rounded_rect(canvas, cx - gap - bar, cy - tall * 0.5f, cx - gap, cy + tall * 0.5f,
                     pxf(canvas, 4.0f), colours.white, 1.0f);
        rounded_rect(canvas, cx + gap, cy - tall * 0.5f, cx + gap + bar, cy + tall * 0.5f,
                     pxf(canvas, 4.0f), colours.white, 1.0f);
    }

    const osd_text_style_t title = text_style(px(canvas, 40.0f));
    const osd_text_style_t body = text_style(px(canvas, 28.0f));
    const osd_text_style_t small = text_style(px(canvas, 22.0f));
    const int title_middle = height - px(canvas, 268.0f);
    const int subtitle_middle = height - px(canvas, 222.0f);
    const int detail_width = full_width(body, state->detail);
    const int detail_space = detail_width < width / 3 ? detail_width : width / 3;
    draw_text(canvas, title, margin, title_middle,
              width - 2 * margin - detail_space - px(canvas, 40.0f), ALIGN_LEFT, state->title,
              colours.white);
    draw_text(canvas, body, margin, subtitle_middle,
              width - 2 * margin - detail_space - px(canvas, 40.0f), ALIGN_LEFT, state->subtitle,
              colours.soft);
    draw_text(canvas, body, width - margin, state->subtitle[0] ? subtitle_middle : title_middle,
              detail_space, ALIGN_RIGHT, state->detail, colours.dim);

    const int bar_middle = height - px(canvas, 150.0f);
    draw_timeline(canvas, margin, width - margin, bar_middle, state->position_us,
                  state->duration_us, state->seeking != 0, 1);

    const int times_middle = height - px(canvas, 108.0f);
    char elapsed[24];
    iptv_osd_format_time(state->position_us, elapsed, sizeof(elapsed));
    draw_text(canvas, body, margin, times_middle, width / 3, ALIGN_LEFT, elapsed, colours.white);
    int right = width - margin;
    if (state->clock[0])
    {
        right -= draw_text(canvas, small, right, times_middle, width / 4, ALIGN_RIGHT, state->clock,
                           colours.dim);
        right -= px(canvas, 24.0f);
    }
    if (state->duration_us > 0)
    {
        char remaining[24] = "-";
        const int64_t left = state->duration_us - (state->position_us > 0 ? state->position_us : 0);
        iptv_osd_format_time(left > 0 ? left : 0, remaining + 1, sizeof(remaining) - 1u);
        draw_text(canvas, body, right, times_middle, width / 4, ALIGN_RIGHT, remaining,
                  colours.white);
    }

    int x = margin;
    const int hints_middle = height - px(canvas, 46.0f);
    if (state->buttons & IPTV_OSD_BUTTON_PAUSE)
        x = draw_hint(canvas, x, hints_middle, ICON_CROSS, state->paused ? "Play" : "Pause");
    if (state->buttons & IPTV_OSD_BUTTON_SEEK)
        x = draw_hint(canvas, x, hints_middle, ICON_LEFT_RIGHT, "10 s");
    if (state->buttons & IPTV_OSD_BUTTON_JUMP)
        x = draw_hint(canvas, x, hints_middle, ICON_UP_DOWN, "1 min");
    if (state->buttons & IPTV_OSD_BUTTON_AUDIO)
        x = draw_hint(canvas, x, hints_middle, ICON_TRIANGLE, "Audio");
    if (state->buttons & IPTV_OSD_BUTTON_RESTART)
        x = draw_hint(canvas, x, hints_middle, ICON_SQUARE, "Start over");
    if (state->buttons & IPTV_OSD_BUTTON_BACK)
        (void)draw_hint(canvas, x, hints_middle, ICON_CIRCLE, "Back");
    return top;
}

static int draw_live(osd_canvas_t *canvas, const iptv_osd_state_t *state)
{
    const iptv_osd_surface_t *surface = canvas->surface;
    const osd_palette_t colours = palette();
    const int width = (int)surface->width;
    const int height = (int)surface->height;
    const int margin = px(canvas, 96.0f);
    const int top = backdrop_top(canvas);
    draw_backdrop(canvas, top);

    const osd_text_style_t title = text_style(px(canvas, 40.0f));
    const osd_text_style_t body = text_style(px(canvas, 28.0f));
    const osd_text_style_t small = text_style(px(canvas, 22.0f));

    /* A red LIVE pill, the channel and the time. */
    const int head_middle = height - px(canvas, 276.0f);
    const float pill_height = pxf(canvas, 38.0f);
    const float pill_width = (float)full_width(small, "LIVE") + pxf(canvas, 30.0f);
    rounded_rect(canvas, (float)margin, (float)head_middle - pill_height * 0.5f,
                 (float)margin + pill_width, (float)head_middle + pill_height * 0.5f,
                 pill_height * 0.5f, colours.red, 1.0f);
    draw_text(canvas, small, margin + (int)(pill_width * 0.5f), head_middle, (int)pill_width,
              ALIGN_CENTRE, "LIVE", colours.white);
    const int clock_width = draw_text(canvas, title, width - margin, head_middle, width / 4,
                                      ALIGN_RIGHT, state->clock, colours.white);
    const int name_left = margin + (int)pill_width + px(canvas, 22.0f);
    draw_text(canvas, title, name_left, head_middle,
              width - margin - clock_width - px(canvas, 40.0f) - name_left, ALIGN_LEFT,
              state->title, colours.white);

    /* The programme on now, with how far through it is. */
    const int now_middle = height - px(canvas, 206.0f);
    draw_text(canvas, body, margin, now_middle, width - 2 * margin, ALIGN_LEFT,
              state->detail[0] ? state->detail : "No guide information",
              state->detail[0] ? colours.white : colours.dim);
    if (state->duration_us > 0)
    {
        const int bar_middle = height - px(canvas, 156.0f);
        const int label_space = px(canvas, 110.0f);
        draw_text(canvas, small, margin, bar_middle, label_space, ALIGN_LEFT, state->start_label,
                  colours.dim);
        draw_text(canvas, small, width - margin, bar_middle, label_space, ALIGN_RIGHT,
                  state->end_label, colours.dim);
        draw_timeline(canvas, margin + label_space, width - margin - label_space, bar_middle,
                      state->position_us, state->duration_us, 0, 0);
    }
    draw_text(canvas, body, margin, height - px(canvas, 96.0f), width - 2 * margin, ALIGN_LEFT,
              state->next, colours.dim);
    return top;
}

static float canvas_scale(const iptv_osd_surface_t *surface)
{
    float scale = (float)surface->width / 1920.0f;
    /* A frame much wider than 16:9 still gets controls sized for its height. */
    if ((float)surface->height / 1080.0f < scale * 0.75f)
        scale = (float)surface->height / 1080.0f / 0.75f;
    return scale;
}

/* Whether the surface can be drawn on: both planes present and big enough. */
static int surface_usable(const iptv_osd_surface_t *surface)
{
    if (!surface || !surface->data || !surface->mask ||
        (surface->component_bytes != 1u && surface->component_bytes != 2u) || (surface->x & 1u) ||
        (surface->y & 1u) || surface->x + surface->width > surface->pitch ||
        surface->y + surface->height > surface->surface_height)
        return 0;
    const size_t samples = (size_t)surface->pitch * surface->surface_height +
                           (size_t)surface->pitch * ((surface->surface_height + 1u) / 2u);
    return samples * surface->component_bytes <= surface->bytes;
}

static osd_canvas_t make_canvas(const iptv_osd_surface_t *surface)
{
    osd_canvas_t canvas = {
        surface,
        1.0f,
        osd_rgb(0x0b, 0x09, 0x10),
        (size_t)surface->pitch * surface->surface_height,
        surface->x,
        surface->y,
    };
    canvas.scale = canvas_scale(surface);
    return canvas;
}

void iptv_osd_clear(const iptv_osd_surface_t *surface, uint32_t first_row, uint32_t rows)
{
    if (!surface_usable(surface) || first_row >= surface->surface_height)
        return;
    if (rows > surface->surface_height - first_row)
        rows = surface->surface_height - first_row;
    const size_t pitch = surface->pitch;
    const uint32_t bytes = surface->component_bytes;
    const size_t chroma = pitch * surface->surface_height;
    const uint32_t chroma_first = first_row / 2u;
    const uint32_t chroma_end = (first_row + rows + 1u) / 2u;
    uint8_t *planes[2] = {surface->data, surface->mask};
    for (int plane = 0; plane < 2; ++plane)
    {
        uint8_t *data = planes[plane];
        if (bytes == 1u)
        {
            memset(data + (size_t)first_row * pitch, 16, (size_t)rows * pitch);
            memset(data + chroma + (size_t)chroma_first * pitch, 128,
                   (size_t)(chroma_end - chroma_first) * pitch);
            continue;
        }
        uint16_t *words = (uint16_t *)data;
        for (size_t index = (size_t)first_row * pitch; index < (size_t)(first_row + rows) * pitch;
             ++index)
            words[index] = 64;
        for (size_t index = chroma + (size_t)chroma_first * pitch;
             index < chroma + (size_t)chroma_end * pitch; ++index)
            words[index] = 512;
    }
}

int iptv_osd_draw_stats(const iptv_osd_surface_t *surface, const char *text, uint32_t *first_row,
                        uint32_t *rows)
{
    if (!surface_usable(surface) || !text || !*text || surface->width < 320u ||
        surface->height < 180u)
        return -1;
    osd_canvas_t canvas = make_canvas(surface);
    const osd_palette_t colours = palette();
    const osd_text_style_t style = text_style(px(&canvas, 22.0f));
    const float left = pxf(&canvas, 32.0f);
    const float top = pxf(&canvas, 28.0f);
    const float height = pxf(&canvas, 44.0f);
    const float width = fminf((float)full_width(style, text) + pxf(&canvas, 36.0f),
                              (float)surface->width - 2.0f * left);
    rounded_rect(&canvas, left, top, left + width, top + height, height * 0.5f, colours.dark,
                 0.78f);
    draw_text(&canvas, style, (int)(left + pxf(&canvas, 18.0f)), (int)(top + height * 0.5f),
              (int)(width - pxf(&canvas, 36.0f)), ALIGN_LEFT, text, colours.soft);
    const uint32_t first = ((uint32_t)top & ~1u) > 2u ? ((uint32_t)top & ~1u) - 2u : 0u;
    if (first_row)
        *first_row = first + surface->y;
    if (rows)
        *rows = (uint32_t)(top + height) + 4u - first;
    return 0;
}

int iptv_osd_draw(const iptv_osd_surface_t *surface, const iptv_osd_state_t *state,
                  uint32_t *first_row, uint32_t *rows)
{
    if (!state || state->kind == IPTV_OSD_HIDDEN || !surface_usable(surface) ||
        surface->width < 320u || surface->height < 180u)
        return -1;
    osd_canvas_t canvas = make_canvas(surface);
    const int top =
        state->kind == IPTV_OSD_LIVE ? draw_live(&canvas, state) : draw_media(&canvas, state);
    /* The pause badge sits above the backdrop. */
    int first = state->kind == IPTV_OSD_MEDIA && state->paused
                    ? (int)((float)surface->height * 0.42f - pxf(&canvas, 80.0f)) & ~1
                    : top;
    if (first < 0)
        first = 0;
    if (first_row)
        *first_row = (uint32_t)first + surface->y;
    if (rows)
        *rows = surface->height - (uint32_t)first;
    return 0;
}

void iptv_osd_format_time(int64_t microseconds, char *out, size_t capacity)
{
    if (!out || !capacity)
        return;
    if (microseconds < 0)
    {
        snprintf(out, capacity, "--:--");
        return;
    }
    const int64_t seconds = microseconds / 1000000;
    const int64_t hours = seconds / 3600;
    if (hours)
        snprintf(out, capacity, "%lld:%02lld:%02lld", (long long)hours,
                 (long long)(seconds / 60 % 60), (long long)(seconds % 60));
    else
        snprintf(out, capacity, "%lld:%02lld", (long long)(seconds / 60),
                 (long long)(seconds % 60));
}

/* A sequence lock: the writer makes the sequence odd while it copies. */
static _Atomic uint32_t osd_sequence;
static iptv_osd_state_t osd_published;

void iptv_osd_publish(const iptv_osd_state_t *state)
{
    if (!state)
        return;
    const uint32_t start = atomic_load_explicit(&osd_sequence, memory_order_relaxed);
    atomic_store_explicit(&osd_sequence, start + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    memcpy(&osd_published, state, sizeof(osd_published));
    atomic_store_explicit(&osd_sequence, start + 2u, memory_order_release);
}

uint32_t iptv_osd_snapshot(iptv_osd_state_t *state)
{
    for (;;)
    {
        const uint32_t before = atomic_load_explicit(&osd_sequence, memory_order_acquire);
        if (before & 1u)
            continue;
        if (state)
            memcpy(state, &osd_published, sizeof(*state));
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&osd_sequence, memory_order_relaxed) == before)
            return before;
    }
}

void iptv_osd_hide(void)
{
    iptv_osd_state_t hidden;
    memset(&hidden, 0, sizeof(hidden));
    iptv_osd_publish(&hidden);
}
