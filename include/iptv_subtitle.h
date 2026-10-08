/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_SUBTITLE_H
#define IPTV_SUBTITLE_H

#include "iptv_osd.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Subtitles for the playing file. The player thread adds cues as FFmpeg decodes them (text, or
 * pictures for Blu-ray, DVD and DVB subtitles) and renders the ones due soon into images at the
 * screen overlay's size; the video thread blends the cues showing at each frame's time into the
 * overlay, with the on-screen controls. Times are the player's (normalised) presentation times. */

typedef struct iptv_subtitle_rect
{
    int32_t x; /* on the subtitle canvas */
    int32_t y;
    uint32_t width;
    uint32_t height;
    const uint32_t *argb; /* width * height pixels, 0xAARRGGBB, not premultiplied */
} iptv_subtitle_rect_t;

/* ---- Player thread ---- */

/* Drops every cue (a seek, another track, or the end of playback). */
void iptv_subtitle_reset(void);
/* A text cue: FFmpeg's ASS event line when `ass` is set ("ReadOrder,Layer,Style,...,Text"),
 * otherwise plain text. end_us < 0: shown until the next cue starts (at most 10 s). */
void iptv_subtitle_add_text(int64_t start_us, int64_t end_us, const char *text, int ass);
/* A picture cue on a canvas_width x canvas_height canvas. No rectangles: clears the screen
 * from start_us. end_us < 0: shown until the next cue starts. */
void iptv_subtitle_add_bitmaps(int64_t start_us, int64_t end_us, uint32_t canvas_width,
                               uint32_t canvas_height, const iptv_subtitle_rect_t *rects,
                               uint32_t count);
/* Forgets cues that have ended at `position_us`, and renders the ones due in the next few
 * seconds for an overlay of frame_width x frame_height (IPTV_OSD_OVERLAY_WIDTH x _HEIGHT).
 * Call it regularly while playing. */
void iptv_subtitle_prepare(uint32_t frame_width, uint32_t frame_height, int64_t position_us);

/* ---- Video thread ---- */

/* Blends the cues showing at `pts_us` into the screen overlay `surface` (iptv_osd.h), within
 * its area (x, y, width, height). `picture` is where the picture is on the overlay (x, y,
 * width, height; it may reach past the edges, or be NULL for the whole area): text goes at the
 * bottom (or top) of the part of it on screen, pictures where their canvas puts them. The
 * bottom `reserved_bottom` rows of the area are kept clear for the controls. Reports the rows
 * written. Returns 0, or -1 when nothing was drawn. */
int iptv_subtitle_draw(const iptv_osd_surface_t *surface, int64_t pts_us,
                       const int32_t picture[4], uint32_t reserved_bottom, uint32_t *first_row,
                       uint32_t *rows);

/* Changes whenever the cues showing at `pts_us` (or their images) change; 0 for none. */
uint64_t iptv_subtitle_signature(int64_t pts_us);

/* ---- Exposed for tests ---- */

/* Plain text for drawing: ASS override blocks and HTML-style tags removed, \N and line breaks
 * as '\n', \h as a space, and italics marked with '\x01' (on) and '\x02' (off). *top is set
 * when the cue asks to be at the top of the picture ({\an7}, {\an8}, {\an9}). Returns the
 * length written. */
size_t iptv_subtitle_plain_text(const char *text, int ass, char *out, size_t capacity, int *top);

/* Decodes one UTF-8 character; returns its length (at least 1), with U+FFFD for bad input. */
size_t iptv_subtitle_utf8(const char *text, uint32_t *code);

/* The number of cues held, and how many of them have a rendered image. */
void iptv_subtitle_counts(uint32_t *cues, uint32_t *rendered);

#ifdef __cplusplus
}
#endif

#endif
