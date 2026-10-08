/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_subtitle.h"

#include "iptv_overlay_blend.h"
#include "iptv_subtitle_font.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CUES 128u
#define OPEN_CUE_LIMIT_US INT64_C(10000000)
#define RENDER_AHEAD_US INT64_C(6000000)
#define KEEP_AFTER_US INT64_C(1000000)
#define TEXT_BYTES 2048u
#define ITALIC_ON '\x01'
#define ITALIC_OFF '\x02'

/* ---- Text clean-up ---- */

size_t iptv_subtitle_utf8(const char *text, uint32_t *code)
{
    const unsigned char *bytes = (const unsigned char *)text;
    const unsigned char first = bytes[0];
    uint32_t value;
    size_t length;
    if (first < 0x80u)
    {
        *code = first;
        return 1;
    }
    if ((first & 0xe0u) == 0xc0u)
    {
        value = first & 0x1fu;
        length = 2;
    }
    else if ((first & 0xf0u) == 0xe0u)
    {
        value = first & 0x0fu;
        length = 3;
    }
    else if ((first & 0xf8u) == 0xf0u)
    {
        value = first & 0x07u;
        length = 4;
    }
    else
    {
        *code = 0xfffdu;
        return 1;
    }
    for (size_t index = 1; index < length; ++index)
    {
        if ((bytes[index] & 0xc0u) != 0x80u)
        {
            *code = 0xfffdu;
            return index;
        }
        value = (value << 6) | (bytes[index] & 0x3fu);
    }
    *code = value;
    return length;
}

typedef struct text_writer
{
    char *out;
    size_t capacity;
    size_t length;
} text_writer_t;

static void put_char(text_writer_t *writer, char character)
{
    if (writer->length + 1u < writer->capacity)
        writer->out[writer->length++] = character;
}

/* Reads one ASS override block ({...}) for the tags this player honours. */
static const char *read_override(const char *at, text_writer_t *writer, int *top, int *drawing)
{
    ++at; /* '{' */
    while (*at && *at != '}')
    {
        if (*at != '\\')
        {
            ++at;
            continue;
        }
        ++at;
        if (at[0] == 'i' && (at[1] == '0' || at[1] == '1'))
        {
            put_char(writer, at[1] == '1' ? ITALIC_ON : ITALIC_OFF);
            at += 2;
        }
        else if (at[0] == 'i' && (at[1] == '\\' || at[1] == '}'))
        {
            put_char(writer, ITALIC_OFF);
            ++at;
        }
        else if (at[0] == 'a' && at[1] == 'n' && at[2] >= '1' && at[2] <= '9')
        {
            if (top)
                *top = at[2] >= '7';
            at += 3;
        }
        else if (at[0] == 'p' && at[1] >= '0' && at[1] <= '9')
        {
            *drawing = at[1] != '0';
            at += 2;
        }
    }
    return *at == '}' ? at + 1 : at;
}

/* Skips an HTML-style tag (<i>, </i>, <font ...>), noting italics. */
static const char *read_tag(const char *at, text_writer_t *writer)
{
    const char *end = strchr(at, '>');
    if (!end || end - at > 64)
        return NULL;
    if ((at[1] == 'i' || at[1] == 'I') && at[2] == '>')
        put_char(writer, ITALIC_ON);
    else if (at[1] == '/' && (at[2] == 'i' || at[2] == 'I') && at[3] == '>')
        put_char(writer, ITALIC_OFF);
    return end + 1;
}

size_t iptv_subtitle_plain_text(const char *text, int ass, char *out, size_t capacity, int *top)
{
    if (top)
        *top = 0;
    if (!out || !capacity)
        return 0;
    out[0] = '\0';
    if (!text)
        return 0;
    if (ass)
    {
        /* ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text */
        for (int commas = 0; commas < 8 && *text; ++text)
            if (*text == ',')
                ++commas;
    }
    text_writer_t writer = {out, capacity, 0};
    int drawing = 0;
    while (*text)
    {
        if (*text == '{')
        {
            text = read_override(text, &writer, top, &drawing);
            continue;
        }
        if (*text == '<')
        {
            const char *after = read_tag(text, &writer);
            if (after)
            {
                text = after;
                continue;
            }
        }
        if (*text == '\\' && (text[1] == 'N' || text[1] == 'n'))
        {
            put_char(&writer, '\n');
            text += 2;
            continue;
        }
        if (*text == '\\' && text[1] == 'h')
        {
            put_char(&writer, ' ');
            text += 2;
            continue;
        }
        if (*text == '\r')
        {
            ++text;
            continue;
        }
        if (!drawing)
            put_char(&writer, *text == '\t' ? ' ' : *text);
        ++text;
    }
    out[writer.length] = '\0';

    /* Trim each line and drop empty ones. */
    size_t read = 0, write = 0;
    while (read < writer.length)
    {
        size_t end = read;
        while (end < writer.length && out[end] != '\n')
            ++end;
        size_t first = read, last = end;
        while (first < last && out[first] == ' ')
            ++first;
        while (last > first && out[last - 1u] == ' ')
            --last;
        int visible = 0;
        for (size_t index = first; index < last; ++index)
            if (out[index] != ITALIC_ON && out[index] != ITALIC_OFF && out[index] != ' ')
                visible = 1;
        if (visible)
        {
            if (write)
                out[write++] = '\n';
            memmove(out + write, out + first, last - first);
            write += last - first;
        }
        else
        {
            /* Keep the style change of an empty line. */
            for (size_t index = first; index < last; ++index)
                if (out[index] == ITALIC_ON || out[index] == ITALIC_OFF)
                    out[write++] = out[index];
        }
        read = end + 1u;
    }
    out[write] = '\0';
    return write;
}

/* ---- The font ---- */

static uint8_t *font_bitmap;

static const uint8_t *font_pixels(void)
{
    /* Unpacked once, on the player thread, before the first cue is rendered. */
    if (font_bitmap)
        return font_bitmap;
    uint8_t *bitmap = malloc(IPTV_SUBTITLE_FONT_BITMAP_BYTES);
    if (!bitmap)
        return NULL;
    size_t at = 0;
    for (size_t index = 0;
         index < IPTV_SUBTITLE_FONT_PACKED_BYTES && at < IPTV_SUBTITLE_FONT_BITMAP_BYTES; ++index)
    {
        const uint8_t value = iptv_subtitle_font_packed[index];
        if (value == 0 && index + 1u < IPTV_SUBTITLE_FONT_PACKED_BYTES)
        {
            size_t run = iptv_subtitle_font_packed[++index];
            if (run > IPTV_SUBTITLE_FONT_BITMAP_BYTES - at)
                run = IPTV_SUBTITLE_FONT_BITMAP_BYTES - at;
            memset(bitmap + at, 0, run);
            at += run;
        }
        else
        {
            bitmap[at++] = value;
        }
    }
    font_bitmap = bitmap;
    return font_bitmap;
}

static const iptv_subtitle_glyph_t *glyph_for(uint32_t code)
{
    size_t low = 0, high = IPTV_SUBTITLE_FONT_GLYPHS;
    while (low < high)
    {
        const size_t middle = (low + high) / 2u;
        if (iptv_subtitle_glyphs[middle].code < code)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low < IPTV_SUBTITLE_FONT_GLYPHS && iptv_subtitle_glyphs[low].code == code)
        return &iptv_subtitle_glyphs[low];
    /* Typographic look-alikes, then a question mark. */
    if (code == 0x00a0u || code == 0x2007u || code == 0x202fu)
        return glyph_for(' ');
    if (code != '?')
        return glyph_for('?');
    return &iptv_subtitle_glyphs[0];
}

/* ---- Rendered images (10-bit BT.709 limited range, alpha out of 255) ---- */

typedef struct image
{
    int32_t x; /* pictures: where on the frame */
    int32_t y;
    uint32_t width; /* even */
    uint32_t height;
    uint16_t *luma;
    uint8_t *alpha;
    uint16_t *chroma; /* interleaved U, V per 2x2 block */
    uint8_t *chroma_alpha;
} image_t;

static void image_free(image_t *image)
{
    if (!image)
        return;
    free(image->luma);
    free(image->alpha);
    free(image->chroma);
    free(image->chroma_alpha);
    free(image);
}

static image_t *image_new(uint32_t width, uint32_t height)
{
    width = (width + 1u) & ~1u;
    height = (height + 1u) & ~1u;
    if (!width || !height || width > 8192u || height > 8192u)
        return NULL;
    image_t *image = calloc(1, sizeof(*image));
    if (!image)
        return NULL;
    const size_t pixels = (size_t)width * height;
    image->width = width;
    image->height = height;
    image->luma = calloc(pixels, sizeof(uint16_t));
    image->alpha = calloc(pixels, 1);
    image->chroma = calloc(pixels / 2u, sizeof(uint16_t));
    image->chroma_alpha = calloc(pixels / 4u, 1);
    if (!image->luma || !image->alpha || !image->chroma || !image->chroma_alpha)
    {
        image_free(image);
        return NULL;
    }
    return image;
}

static void rgb_to_yuv(float red, float green, float blue, int *y, int *u, int *v)
{
    const float luma = 0.2126f * red + 0.7152f * green + 0.0722f * blue;
    *y = (int)(64.0f + 876.0f * luma + 0.5f);
    *u = (int)(512.0f + 896.0f * (blue - luma) / 1.8556f + 0.5f);
    *v = (int)(512.0f + 896.0f * (red - luma) / 1.5748f + 0.5f);
}

/* Fills the 2x2 chroma blocks from full-resolution U and V, weighted by alpha. */
static void image_finish_chroma(image_t *image, const uint16_t *u, const uint16_t *v)
{
    const uint32_t width = image->width;
    for (uint32_t cy = 0; cy < image->height / 2u; ++cy)
        for (uint32_t cx = 0; cx < width / 2u; ++cx)
        {
            uint32_t alpha = 0, sum_u = 0, sum_v = 0;
            for (uint32_t dy = 0; dy < 2u; ++dy)
                for (uint32_t dx = 0; dx < 2u; ++dx)
                {
                    const size_t index = (size_t)(cy * 2u + dy) * width + cx * 2u + dx;
                    const uint32_t a = image->alpha[index];
                    alpha += a;
                    sum_u += a * (u ? u[index] : 512u);
                    sum_v += a * (v ? v[index] : 512u);
                }
            const size_t index = (size_t)cy * (width / 2u) + cx;
            image->chroma_alpha[index] = (uint8_t)(alpha / 4u);
            image->chroma[index * 2u] = (uint16_t)(alpha ? sum_u / alpha : 512u);
            image->chroma[index * 2u + 1u] = (uint16_t)(alpha ? sum_v / alpha : 512u);
        }
}

/* ---- Text rendering: white letters with a dark outline ---- */

typedef struct text_glyph
{
    const iptv_subtitle_glyph_t *glyph;
    uint8_t italic;
} text_glyph_t;

typedef struct text_line
{
    size_t first;
    size_t count;
    float width;
} text_line_t;

static float glyph_advance(const text_glyph_t *glyph, float scale)
{
    return (float)glyph->glyph->advance * scale;
}

static float span_width(const text_glyph_t *glyphs, size_t first, size_t count, float scale)
{
    float width = 0;
    for (size_t index = first; index < first + count; ++index)
        width += glyph_advance(&glyphs[index], scale);
    return width;
}

static int is_space(const text_glyph_t *glyph)
{
    return glyph->glyph->code == ' ';
}

/* Breaks the text into lines no wider than max_width, at spaces where possible. */
static size_t layout_lines(const text_glyph_t *glyphs, const uint8_t *breaks, size_t count,
                           float scale, float max_width, text_line_t *lines, size_t max_lines)
{
    size_t line_count = 0;
    size_t start = 0;
    while (start < count && line_count < max_lines)
    {
        size_t end = start;
        size_t last_space = (size_t)-1;
        float width = 0;
        while (end < count && !breaks[end])
        {
            const float advance = glyph_advance(&glyphs[end], scale);
            if (width + advance > max_width && end > start)
                break;
            if (is_space(&glyphs[end]))
                last_space = end;
            width += advance;
            ++end;
        }
        size_t next = end;
        if (end < count && !breaks[end] && last_space != (size_t)-1 && last_space > start)
        {
            end = last_space;
            next = last_space + 1u;
        }
        else if (end < count && breaks[end])
        {
            next = end + 1u;
        }
        size_t first = start, last = end;
        while (first < last && is_space(&glyphs[first]))
            ++first;
        while (last > first && is_space(&glyphs[last - 1u]))
            --last;
        if (last > first)
        {
            lines[line_count].first = first;
            lines[line_count].count = last - first;
            lines[line_count].width = span_width(glyphs, first, last - first, scale);
            ++line_count;
        }
        start = next;
    }
    return line_count;
}

static float sample_glyph(const uint8_t *pixels, const iptv_subtitle_glyph_t *glyph, float x,
                          float y)
{
    /* Bilinear, with zero outside the glyph. */
    const float fx = floorf(x), fy = floorf(y);
    const int x0 = (int)fx, y0 = (int)fy;
    const float tx = x - fx, ty = y - fy;
    float result = 0;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx)
        {
            const int sx = x0 + dx, sy = y0 + dy;
            if (sx < 0 || sy < 0 || sx >= glyph->width || sy >= glyph->height)
                continue;
            const float weight = (dx ? tx : 1.0f - tx) * (dy ? ty : 1.0f - ty);
            result += weight * pixels[glyph->offset + (size_t)sy * glyph->width + (size_t)sx];
        }
    return result;
}

static image_t *render_text(const char *text, uint32_t frame_width, uint32_t frame_height)
{
    const uint8_t *pixels = font_pixels();
    if (!pixels || !text || !*text)
        return NULL;

    /* Characters to glyphs, with the line breaks and italics the text asks for. */
    size_t capacity = strlen(text) + 1u;
    text_glyph_t *glyphs = calloc(capacity, sizeof(*glyphs));
    uint8_t *breaks = calloc(capacity, 1);
    text_line_t lines[16];
    if (!glyphs || !breaks)
    {
        free(glyphs);
        free(breaks);
        return NULL;
    }
    size_t count = 0;
    uint8_t italic = 0;
    for (const char *at = text; *at;)
    {
        if (*at == ITALIC_ON || *at == ITALIC_OFF)
        {
            italic = *at == ITALIC_ON;
            ++at;
            continue;
        }
        if (*at == '\n')
        {
            glyphs[count].glyph = glyph_for(' ');
            breaks[count++] = 1;
            ++at;
            continue;
        }
        uint32_t code = 0;
        at += iptv_subtitle_utf8(at, &code);
        if (code < 0x20u)
            continue;
        glyphs[count].glyph = glyph_for(code);
        glyphs[count++].italic = italic;
    }

    /* About 1/19 of the picture's height per line of capitals; less on a tall narrow frame. */
    float size = (float)frame_height * 0.052f;
    if (size > (float)frame_width * 0.034f)
        size = (float)frame_width * 0.034f;
    if (size < 14.0f)
        size = 14.0f;
    const float scale = size / (float)IPTV_SUBTITLE_FONT_SIZE;
    const float line_height =
        (float)(IPTV_SUBTITLE_FONT_ASCENT + IPTV_SUBTITLE_FONT_DESCENT) * scale * 1.02f;
    const float outline = fmaxf(1.5f, size * 0.065f);
    const float slant = 0.2f;
    const int pad = (int)ceilf(outline) + 2;
    const size_t line_count =
        layout_lines(glyphs, breaks, count, scale, (float)frame_width * 0.86f, lines, 16u);
    if (!line_count)
    {
        free(glyphs);
        free(breaks);
        return NULL;
    }
    float widest = 0;
    for (size_t index = 0; index < line_count; ++index)
        widest = fmaxf(widest, lines[index].width);
    const uint32_t width = (uint32_t)(widest + size * slant) + 2u * (uint32_t)pad;
    const uint32_t height = (uint32_t)ceilf(line_height * (float)line_count) + 2u * (uint32_t)pad;
    image_t *image = image_new(width, height);
    uint8_t *fill = image ? calloc((size_t)image->width * image->height, 1) : NULL;
    if (!fill)
    {
        image_free(image);
        free(glyphs);
        free(breaks);
        return NULL;
    }
    const int image_width = (int)image->width, image_height = (int)image->height;

    /* The letters' coverage. */
    for (size_t line = 0; line < line_count; ++line)
    {
        float pen = ((float)image_width - lines[line].width) * 0.5f;
        const float top = (float)pad + line_height * (float)line;
        const float baseline = top + (float)IPTV_SUBTITLE_FONT_ASCENT * scale;
        for (size_t index = lines[line].first; index < lines[line].first + lines[line].count;
             ++index)
        {
            const iptv_subtitle_glyph_t *glyph = glyphs[index].glyph;
            const float shear = glyphs[index].italic ? slant : 0.0f;
            if (glyph->width && glyph->height)
            {
                const float left = pen + (float)glyph->x_offset * scale;
                const float upper = top + (float)glyph->y_offset * scale;
                const float glyph_width = (float)glyph->width * scale;
                const float glyph_height = (float)glyph->height * scale;
                const int row_first = (int)floorf(upper) - 1;
                const int row_last = (int)ceilf(upper + glyph_height) + 1;
                for (int y = row_first; y <= row_last; ++y)
                {
                    if (y < 0 || y >= image_height)
                        continue;
                    const float offset = ((baseline - (float)y) * shear);
                    const int column_first = (int)floorf(left + offset) - 1;
                    const int column_last = (int)ceilf(left + offset + glyph_width) + 1;
                    for (int x = column_first; x <= column_last; ++x)
                    {
                        if (x < 0 || x >= image_width)
                            continue;
                        const float sx = ((float)x + 0.5f - left - offset) / scale - 0.5f;
                        const float sy = ((float)y + 0.5f - upper) / scale - 0.5f;
                        const float value = sample_glyph(pixels, glyph, sx, sy);
                        uint8_t *target = &fill[(size_t)y * (size_t)image_width + (size_t)x];
                        const int coverage = (int)(value + 0.5f);
                        if (coverage > *target)
                            *target = (uint8_t)(coverage > 255 ? 255 : coverage);
                    }
                }
            }
            pen += glyph_advance(&glyphs[index], scale);
        }
    }

    /* The outline: the coverage grown by a disc, soft at its edge. */
    const int reach = (int)ceilf(outline);
    for (int y = 0; y < image_height; ++y)
        for (int x = 0; x < image_width; ++x)
        {
            float strongest = 0;
            for (int dy = -reach; dy <= reach; ++dy)
            {
                const int sy = y + dy;
                if (sy < 0 || sy >= image_height)
                    continue;
                for (int dx = -reach; dx <= reach; ++dx)
                {
                    const int sx = x + dx;
                    if (sx < 0 || sx >= image_width)
                        continue;
                    const uint8_t value = fill[(size_t)sy * (size_t)image_width + (size_t)sx];
                    if (!value)
                        continue;
                    const float weight = fminf(
                        1.0f, fmaxf(0.0f, outline + 0.5f - sqrtf((float)(dx * dx + dy * dy))));
                    const float candidate = (float)value * weight;
                    if (candidate > strongest)
                        strongest = candidate;
                }
            }
            const size_t index = (size_t)y * (size_t)image_width + (size_t)x;
            const int letter = fill[index];
            int edge = (int)(strongest + 0.5f);
            if (edge < letter)
                edge = letter;
            image->alpha[index] = (uint8_t)edge;
            /* White over a near-black outline. */
            image->luma[index] =
                (uint16_t)(edge ? 80 + (int)((long)(920 - 80) * letter / edge) : 64);
        }
    image_finish_chroma(image, NULL, NULL);
    free(fill);
    free(glyphs);
    free(breaks);
    return image;
}

/* ---- Picture subtitles ---- */

static image_t *render_bitmap(const uint32_t *argb, int32_t source_x, int32_t source_y,
                              uint32_t source_width, uint32_t source_height, uint32_t canvas_width,
                              uint32_t canvas_height, uint32_t frame_width, uint32_t frame_height)
{
    if (!argb || !source_width || !source_height)
        return NULL;
    if (!canvas_width || !canvas_height)
    {
        canvas_width = frame_width;
        canvas_height = frame_height;
    }
    /* The canvas spans the frame's width. A canvas taller than the frame (subtitles made
     * for 1920x1080 over a picture cropped to 1920x800) is centred on it. */
    const float scale = (float)frame_width / (float)canvas_width;
    const float shift = ((float)canvas_height * scale - (float)frame_height) * 0.5f;
    const uint32_t width = (uint32_t)ceilf((float)source_width * scale);
    const uint32_t height = (uint32_t)ceilf((float)source_height * scale);
    image_t *image = image_new(width, height);
    if (!image)
        return NULL;
    image->x = (int32_t)floorf((float)source_x * scale) & ~1;
    image->y = (int32_t)floorf((float)source_y * scale - shift) & ~1;
    const size_t pixels = (size_t)image->width * image->height;
    uint16_t *u = malloc(pixels * sizeof(uint16_t));
    uint16_t *v = malloc(pixels * sizeof(uint16_t));
    if (!u || !v)
    {
        free(u);
        free(v);
        image_free(image);
        return NULL;
    }
    for (uint32_t y = 0; y < image->height; ++y)
        for (uint32_t x = 0; x < image->width; ++x)
        {
            /* Bilinear over premultiplied colour. */
            const float sx = ((float)x + 0.5f) / scale - 0.5f;
            const float sy = ((float)y + 0.5f) / scale - 0.5f;
            const float fx = floorf(sx), fy = floorf(sy);
            const float tx = sx - fx, ty = sy - fy;
            float a = 0, r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                {
                    int px = (int)fx + dx, py = (int)fy + dy;
                    if (px < 0)
                        px = 0;
                    if (py < 0)
                        py = 0;
                    if (px >= (int)source_width)
                        px = (int)source_width - 1;
                    if (py >= (int)source_height)
                        py = (int)source_height - 1;
                    const uint32_t pixel = argb[(size_t)py * source_width + (size_t)px];
                    const float weight = (dx ? tx : 1.0f - tx) * (dy ? ty : 1.0f - ty);
                    const float alpha = (float)(pixel >> 24) / 255.0f * weight;
                    a += alpha;
                    r += alpha * (float)((pixel >> 16) & 0xffu) / 255.0f;
                    g += alpha * (float)((pixel >> 8) & 0xffu) / 255.0f;
                    b += alpha * (float)(pixel & 0xffu) / 255.0f;
                }
            const size_t index = (size_t)y * image->width + x;
            int luma = 64, cb = 512, cr = 512;
            if (a > 0.001f)
                rgb_to_yuv(r / a, g / a, b / a, &luma, &cb, &cr);
            image->alpha[index] = (uint8_t)(a * 255.0f + 0.5f);
            image->luma[index] = (uint16_t)luma;
            u[index] = (uint16_t)cb;
            v[index] = (uint16_t)cr;
        }
    image_finish_chroma(image, u, v);
    free(u);
    free(v);
    return image;
}

/* ---- Cues ---- */

typedef struct cue
{
    int64_t start_us;
    int64_t end_us;
    uint8_t open;   /* ends when the next cue starts */
    uint8_t bitmap; /* a picture, else text */
    uint8_t top;
    char *text;
    uint32_t *argb;
    int32_t x, y;
    uint32_t width, height;
    uint32_t canvas_width, canvas_height;
    image_t *image;
    uint32_t image_frame_width, image_frame_height;
    uint32_t serial; /* tells the cues apart, for iptv_subtitle_signature */
} cue_t;

static uint32_t next_serial = 1;

static cue_t cues[MAX_CUES];
static uint32_t cue_count;
static atomic_flag cue_lock = ATOMIC_FLAG_INIT;

static void lock(void)
{
    while (atomic_flag_test_and_set_explicit(&cue_lock, memory_order_acquire))
    {
    }
}

static void unlock(void)
{
    atomic_flag_clear_explicit(&cue_lock, memory_order_release);
}

static void cue_free(cue_t *cue)
{
    free(cue->text);
    free(cue->argb);
    image_free(cue->image);
    memset(cue, 0, sizeof(*cue));
}

/* Removes cues[index]; the caller holds the lock. */
static void remove_cue(uint32_t index)
{
    cue_t removed = cues[index];
    memmove(&cues[index], &cues[index + 1u], (size_t)(cue_count - index - 1u) * sizeof(cue_t));
    --cue_count;
    memset(&cues[cue_count], 0, sizeof(cue_t));
    cue_free(&removed);
}

void iptv_subtitle_reset(void)
{
    lock();
    for (uint32_t index = 0; index < cue_count; ++index)
        cue_free(&cues[index]);
    cue_count = 0;
    unlock();
}

/* Ends the open cues that started before `start_us`, and inserts `cue` in start order. */
static void insert_cue(const cue_t *cue)
{
    lock();
    for (uint32_t index = 0; index < cue_count; ++index)
        if (cues[index].open && cues[index].start_us < cue->start_us &&
            cues[index].end_us > cue->start_us)
            cues[index].end_us = cue->start_us;
    if (cue->text || cue->argb)
    {
        if (cue_count == MAX_CUES)
            remove_cue(0);
        uint32_t at = cue_count;
        while (at > 0 && cues[at - 1u].start_us > cue->start_us)
            --at;
        memmove(&cues[at + 1u], &cues[at], (size_t)(cue_count - at) * sizeof(cue_t));
        cues[at] = *cue;
        cues[at].serial = next_serial++;
        ++cue_count;
    }
    unlock();
}

void iptv_subtitle_add_text(int64_t start_us, int64_t end_us, const char *text, int ass)
{
    char plain[TEXT_BYTES];
    int top = 0;
    cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.start_us = start_us;
    cue.open = end_us < 0 || end_us <= start_us;
    cue.end_us = cue.open ? start_us + OPEN_CUE_LIMIT_US : end_us;
    if (iptv_subtitle_plain_text(text, ass, plain, sizeof(plain), &top))
    {
        cue.text = malloc(strlen(plain) + 1u);
        if (cue.text)
            memcpy(cue.text, plain, strlen(plain) + 1u);
    }
    cue.top = (uint8_t)top;
    insert_cue(&cue);
}

void iptv_subtitle_add_bitmaps(int64_t start_us, int64_t end_us, uint32_t canvas_width,
                               uint32_t canvas_height, const iptv_subtitle_rect_t *rects,
                               uint32_t count)
{
    cue_t cue;
    memset(&cue, 0, sizeof(cue));
    cue.start_us = start_us;
    cue.open = end_us < 0 || end_us <= start_us;
    cue.end_us = cue.open ? INT64_MAX : end_us;
    cue.bitmap = 1;
    cue.canvas_width = canvas_width;
    cue.canvas_height = canvas_height;

    /* One picture covering every rectangle. */
    int32_t left = INT32_MAX, top = INT32_MAX, right = INT32_MIN, bottom = INT32_MIN;
    for (uint32_t index = 0; rects && index < count; ++index)
    {
        const iptv_subtitle_rect_t *rect = &rects[index];
        if (!rect->argb || !rect->width || !rect->height || rect->width > 8192u ||
            rect->height > 8192u)
            continue;
        if (rect->x < left)
            left = rect->x;
        if (rect->y < top)
            top = rect->y;
        if (rect->x + (int32_t)rect->width > right)
            right = rect->x + (int32_t)rect->width;
        if (rect->y + (int32_t)rect->height > bottom)
            bottom = rect->y + (int32_t)rect->height;
    }
    if (right > left && bottom > top && right - left <= 8192 && bottom - top <= 8192)
    {
        cue.x = left;
        cue.y = top;
        cue.width = (uint32_t)(right - left);
        cue.height = (uint32_t)(bottom - top);
        cue.argb = calloc((size_t)cue.width * cue.height, sizeof(uint32_t));
        for (uint32_t index = 0; cue.argb && index < count; ++index)
        {
            const iptv_subtitle_rect_t *rect = &rects[index];
            if (!rect->argb || !rect->width || !rect->height || rect->width > 8192u ||
                rect->height > 8192u)
                continue;
            for (uint32_t row = 0; row < rect->height; ++row)
                for (uint32_t column = 0; column < rect->width; ++column)
                {
                    const uint32_t pixel = rect->argb[(size_t)row * rect->width + column];
                    if (pixel >> 24)
                        cue.argb[(size_t)(rect->y - top + (int32_t)row) * cue.width +
                                 (size_t)(rect->x - left) + column] = pixel;
                }
        }
    }
    insert_cue(&cue);
}

void iptv_subtitle_prepare(uint32_t frame_width, uint32_t frame_height, int64_t position_us)
{
    /* Ended cues go. Only this thread changes the list, so below it reads the list without
     * the lock and takes it only to attach an image. */
    lock();
    for (uint32_t index = 0; index < cue_count;)
    {
        if (position_us >= 0 && cues[index].end_us != INT64_MAX &&
            cues[index].end_us + KEEP_AFTER_US < position_us)
            remove_cue(index);
        else
            ++index;
    }
    unlock();
    if (!frame_width || !frame_height)
        return;

    /* A couple of renders per call keeps the reading loop moving. */
    unsigned rendered = 0;
    for (uint32_t index = 0; index < cue_count && rendered < 2u; ++index)
    {
        cue_t *cue = &cues[index];
        if (cue->image && cue->image_frame_width == frame_width &&
            cue->image_frame_height == frame_height)
            continue;
        if (position_us >= 0 && cue->start_us > position_us + RENDER_AHEAD_US)
            break;
        if ((!cue->text && !cue->argb) || (position_us >= 0 && cue->end_us <= position_us))
            continue;
        image_t *image = cue->bitmap ? render_bitmap(cue->argb, cue->x, cue->y, cue->width,
                                                     cue->height, cue->canvas_width,
                                                     cue->canvas_height, frame_width, frame_height)
                                     : render_text(cue->text, frame_width, frame_height);
        ++rendered;
        if (!image)
        {
            /* Nothing to show: keep the cue for its timing, without a picture. */
            free(cue->text);
            cue->text = NULL;
            free(cue->argb);
            cue->argb = NULL;
            continue;
        }
        lock();
        image_t *old = cue->image;
        cue->image = image;
        cue->image_frame_width = frame_width;
        cue->image_frame_height = frame_height;
        unlock();
        image_free(old);
    }
}

void iptv_subtitle_counts(uint32_t *count, uint32_t *rendered)
{
    lock();
    if (count)
        *count = cue_count;
    if (rendered)
    {
        *rendered = 0;
        for (uint32_t index = 0; index < cue_count; ++index)
            if (cues[index].image)
                ++*rendered;
    }
    unlock();
}

/* ---- Blending into the overlay ---- */

/* Draws `image` with its top-left corner at (x, y), clipped to the surface's area. Returns the
 * rows it covered through first/last. */
static void blend_image(const iptv_osd_surface_t *surface, const image_t *image, int x, int y,
                        int *first_row, int *last_row)
{
    const int area_left = (int)surface->x, area_top = (int)surface->y;
    const int area_right = area_left + (int)surface->width;
    const int area_bottom = area_top + (int)surface->height;
    x &= ~1;
    y &= ~1;
    const int left = x > area_left ? x : area_left;
    const int top = y > area_top ? y : area_top;
    const int right = x + (int)image->width < area_right ? x + (int)image->width : area_right;
    const int bottom = y + (int)image->height < area_bottom ? y + (int)image->height : area_bottom;
    if (left >= right || top >= bottom)
        return;
    const size_t pitch = surface->pitch;
    for (int row = top; row < bottom; ++row)
    {
        const size_t source = (size_t)(row - y) * image->width;
        const size_t target = (size_t)row * pitch;
        for (int column = left; column < right; ++column)
        {
            const size_t index = source + (size_t)(column - x);
            const int alpha = image->alpha[index];
            if (alpha)
                iptv_overlay_luma(surface, target + (size_t)column, image->luma[index],
                                  alpha + (alpha >> 7));
        }
    }
    const size_t chroma_plane = pitch * surface->surface_height;
    const uint32_t chroma_width = image->width / 2u;
    for (int row = top / 2; row < (bottom + 1) / 2; ++row)
    {
        const size_t source = (size_t)(row - y / 2) * chroma_width;
        const size_t target = chroma_plane + (size_t)row * pitch;
        for (int column = left / 2; column < (right + 1) / 2; ++column)
        {
            const size_t index = source + (size_t)(column - x / 2);
            const int alpha = image->chroma_alpha[index];
            if (alpha)
                iptv_overlay_chroma(surface, target + (size_t)column * 2u,
                                    image->chroma[index * 2u], image->chroma[index * 2u + 1u],
                                    alpha + (alpha >> 7));
        }
    }
    if (top < *first_row)
        *first_row = top;
    if (bottom > *last_row)
        *last_row = bottom;
}

static int showing(const cue_t *cue, int64_t pts_us)
{
    return cue->image && cue->start_us <= pts_us && cue->end_us > pts_us;
}

uint64_t iptv_subtitle_signature(int64_t pts_us)
{
    uint64_t signature = UINT64_C(1469598103934665603);
    int any = 0;
    if (pts_us < 0)
        return 0;
    lock();
    for (uint32_t index = 0; index < cue_count; ++index)
        if (showing(&cues[index], pts_us))
        {
            any = 1;
            signature ^= (uint64_t)cues[index].serial * 2u + 1u;
            signature *= UINT64_C(1099511628211);
            signature ^= (uint64_t)(uintptr_t)cues[index].image;
            signature *= UINT64_C(1099511628211);
        }
    unlock();
    return any ? signature : 0;
}

int iptv_subtitle_draw(const iptv_osd_surface_t *surface, int64_t pts_us, const int32_t picture[4],
                       uint32_t reserved_bottom, uint32_t *first_row, uint32_t *rows)
{
    if (!surface || !surface->data || !surface->mask || pts_us < 0 ||
        (surface->component_bytes != 1u && surface->component_bytes != 2u) ||
        surface->x + surface->width > surface->pitch ||
        surface->y + surface->height > surface->surface_height || !surface->height)
        return -1;
    const size_t samples = (size_t)surface->pitch * surface->surface_height +
                           (size_t)surface->pitch * ((surface->surface_height + 1u) / 2u);
    if (samples * surface->component_bytes > surface->bytes)
        return -1;
    if (reserved_bottom > surface->height / 2u)
        reserved_bottom = surface->height / 2u;

    /* Text sits inside the part of the picture on screen, above the controls when they show. */
    const int area_top = (int)surface->y;
    const int area_bottom = (int)(surface->y + surface->height);
    int picture_top = area_top, picture_bottom = area_bottom;
    if (picture && picture[3] > 0)
    {
        picture_top = picture[1] > area_top ? picture[1] : area_top;
        picture_bottom =
            picture[1] + picture[3] < area_bottom ? picture[1] + picture[3] : area_bottom;
        if (picture_bottom - picture_top < (int)surface->height / 4)
        {
            picture_top = area_top;
            picture_bottom = area_bottom;
        }
    }
    const int margin = (int)((float)surface->height * 0.045f);
    int bottom_edge = picture_bottom - margin;
    const int controls_edge = area_bottom - (int)reserved_bottom - margin / 2;
    if (reserved_bottom && controls_edge < bottom_edge)
        bottom_edge = controls_edge;
    int top_edge = picture_top + margin;
    /* Pictures keep their place on the screen, moved up only to clear the controls. */
    const int picture_limit = reserved_bottom ? controls_edge : area_bottom;
    int first = INT32_MAX, last = INT32_MIN;

    lock();
    /* Later cues stack above earlier ones that are still showing. */
    for (uint32_t index = 0; index < cue_count; ++index)
    {
        const cue_t *cue = &cues[index];
        if (!showing(cue, pts_us))
            continue;
        const image_t *image = cue->image;
        if (cue->bitmap)
        {
            int x = image->x + (int)surface->x, y = image->y + (int)surface->y;
            const int area_left = (int)surface->x;
            const int area_right = (int)(surface->x + surface->width);
            if (y + (int)image->height > picture_limit)
                y = picture_limit - (int)image->height;
            if (y < area_top)
                y = area_top;
            if (x + (int)image->width > area_right)
                x = area_right - (int)image->width;
            if (x < area_left)
                x = area_left;
            blend_image(surface, image, x, y, &first, &last);
            continue;
        }
        const int x = (int)surface->x + ((int)surface->width - (int)image->width) / 2;
        if (cue->top)
        {
            blend_image(surface, image, x, top_edge, &first, &last);
            top_edge += (int)image->height;
        }
        else
        {
            bottom_edge -= (int)image->height;
            blend_image(surface, image, x, bottom_edge, &first, &last);
        }
    }
    unlock();
    if (first >= last)
        return -1;
    if (first_row)
        *first_row = (uint32_t)first;
    if (rows)
        *rows = (uint32_t)(last - first);
    return 0;
}
