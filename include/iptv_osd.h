/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_OSD_H
#define IPTV_OSD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The player's on-screen controls. They are drawn by the CPU into a screen overlay that the GPU
 * blends over the picture after placing it, so they keep their size and place whatever the
 * picture's shape or zoom, and the decoded video is never written to.
 *
 * The overlay is two 4:2:0 surfaces in the video's sample format (the Y plane, then interleaved
 * UV at pitch * surface_height; 8-bit, or Main10 with the 10-bit value in the low bits of each
 * 16-bit sample): `data` holds premultiplied colour and `mask` how much each pixel covers the
 * picture (as luma). The presenter darkens the picture by the mask, then adds the colour.
 * Shapes and text are anti-aliased; the controls sit on a gradient towards the bottom. */

/* The overlay's size; the GPU scales it to the screen. */
#define IPTV_OSD_OVERLAY_WIDTH 1920u
#define IPTV_OSD_OVERLAY_HEIGHT 1080u

typedef struct iptv_osd_glyph
{
    uint32_t offset; /* into the font's bitmap */
    uint8_t width;
    uint8_t height;
    int8_t x_offset;
    int8_t y_offset;
    uint8_t advance;
    uint8_t reserved;
} iptv_osd_glyph_t;

typedef struct iptv_osd_font
{
    uint32_t size;
    uint32_t line_height;
    uint32_t base;
    const iptv_osd_glyph_t *glyphs; /* ASCII 32..126 */
    const uint8_t *bitmap;          /* 8-bit coverage */
} iptv_osd_font_t;

typedef struct iptv_osd_surface
{
    uint8_t *data;
    size_t bytes;
    uint32_t pitch; /* in samples */
    uint32_t surface_height;
    uint32_t width; /* the part of the frame on screen */
    uint32_t height;
    uint32_t component_bytes; /* 1 or 2 */
    uint32_t x;               /* where the area to draw in starts (even) */
    uint32_t y;
    uint8_t *mask;            /* the coverage plane, laid out like `data` */
} iptv_osd_surface_t;

enum
{
    IPTV_OSD_HIDDEN = 0,
    IPTV_OSD_MEDIA = 1, /* films and episodes: title, timeline, button hints */
    IPTV_OSD_LIVE = 2,  /* live TV: channel, the programme on now and next, clock */
};

/* The buttons the hint row shows (a bit each). */
enum
{
    IPTV_OSD_BUTTON_PAUSE = 1u << 0,   /* Cross: Pause, or Play when paused */
    IPTV_OSD_BUTTON_SEEK = 1u << 1,    /* Left/Right: 10 s */
    IPTV_OSD_BUTTON_JUMP = 1u << 2,    /* Up/Down: 1 min */
    IPTV_OSD_BUTTON_AUDIO = 1u << 3,   /* Triangle: audio track */
    IPTV_OSD_BUTTON_RESTART = 1u << 4, /* Square: start over */
    IPTV_OSD_BUTTON_BACK = 1u << 5,    /* Circle: back */
};

#define IPTV_OSD_TEXT_BYTES 128u

typedef struct iptv_osd_state
{
    uint32_t kind;
    uint32_t paused;
    uint32_t seeking; /* the timeline shows a target that is not playing yet */
    uint32_t buttons;
    /* Media: the file. Live: the programme on now (start to end). < 0 / <= 0: unknown. */
    int64_t position_us;
    int64_t duration_us;
    char title[IPTV_OSD_TEXT_BYTES];    /* film, series or channel */
    char subtitle[IPTV_OSD_TEXT_BYTES]; /* media: the episode */
    char detail[IPTV_OSD_TEXT_BYTES];   /* media: audio track; live: the programme on now */
    char next[IPTV_OSD_TEXT_BYTES];     /* live: what follows */
    char start_label[16];               /* live: when the programme started and ends */
    char end_label[16];
    char clock[24]; /* live: the time; media: "Ends 21:43" */
} iptv_osd_state_t;

/* Makes `rows` rows from `first_row` transparent. */
void iptv_osd_clear(const iptv_osd_surface_t *surface, uint32_t first_row, uint32_t rows);

/* The statistics line (codec, size, frame rate, bit rate) at the top left. Reports the rows
 * written; returns 0, or -1 when nothing was drawn. */
int iptv_osd_draw_stats(const iptv_osd_surface_t *surface, const char *text, uint32_t *first_row,
                        uint32_t *rows);

/* Draws `state` and reports the rows it wrote, so the caller can flush them. Returns 0, or -1
 * when nothing was drawn (hidden, or the surface is too small). */
int iptv_osd_draw(const iptv_osd_surface_t *surface, const iptv_osd_state_t *state,
                  uint32_t *first_row, uint32_t *rows);

/* "1:02:03" or "42:10"; negative values are written as "--:--". */
void iptv_osd_format_time(int64_t microseconds, char *out, size_t capacity);

/* The player thread publishes the state; the video thread takes a snapshot for each frame.
 * snapshot returns a sequence number that changes whenever a new state is published. */
void iptv_osd_publish(const iptv_osd_state_t *state);
uint32_t iptv_osd_snapshot(iptv_osd_state_t *state);
void iptv_osd_hide(void);

#ifdef __cplusplus
}
#endif

#endif
