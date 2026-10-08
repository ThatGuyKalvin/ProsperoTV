/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_OSD_H
#define IPTV_OSD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
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
        uint8_t *mask; /* the coverage plane, laid out like `data` */
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
        IPTV_OSD_BUTTON_MENU = 1u << 6,    /* Options: the settings menu */
    };

/* The settings menu. Its first page lists the settings; choosing one that has a list of values
 * opens a second page with those values. */
#define IPTV_OSD_MENU_ROWS 8u

    enum
    {
        IPTV_OSD_MENU_SETTINGS = 0, /* the settings and their values */
        IPTV_OSD_MENU_PICKER = 1,   /* one setting's values, the one in use marked */
    };

    enum
    {
        IPTV_OSD_ROW_HEADER = 0,  /* a section title: never chosen */
        IPTV_OSD_ROW_PICKER = 1,  /* opens the values page */
        IPTV_OSD_ROW_STEPPER = 2, /* Left and Right change the value in place */
        IPTV_OSD_ROW_OPTION = 3,  /* a value on the values page */
    };

    /* What a row's picture preview shows (values page). */
    enum
    {
        IPTV_OSD_PREVIEW_NONE = 0,
        IPTV_OSD_PREVIEW_SHAPE = 1,   /* a frame of preview_milli / 1000 width to height */
        IPTV_OSD_PREVIEW_STRETCH = 2, /* the whole screen, whatever the picture's shape */
        IPTV_OSD_PREVIEW_ZOOM = 3,    /* the picture at preview_milli / 1000 of the screen */
    };

    typedef struct iptv_osd_menu_row
    {
        uint32_t kind;
        uint32_t checked; /* the value in use (values page) */
        uint32_t preview;
        uint32_t preview_milli;
        char label[32];
        char value[48]; /* settings page: the value; values page: a note ("Detected") */
    } iptv_osd_menu_row_t;

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
        /* The settings menu, over either layout. */
        uint32_t menu_rows; /* 0: closed */
        uint32_t menu_selected;
        uint32_t menu_page;
        char menu_title[32];
        iptv_osd_menu_row_t menu[IPTV_OSD_MENU_ROWS];
    } iptv_osd_state_t;

    /* Makes `rows` rows from `first_row` transparent. */
    void iptv_osd_clear(const iptv_osd_surface_t *surface, uint32_t first_row, uint32_t rows);

    /* The statistics line (codec, size, frame rate, bit rate) at the top left. Reports the rows
     * written; returns 0, or -1 when nothing was drawn. */
    int iptv_osd_draw_stats(const iptv_osd_surface_t *surface, const char *text,
                            uint32_t *first_row, uint32_t *rows);

    /* Draws `state` and reports the rows it wrote, so the caller can flush them. Returns 0, or -1
     * when nothing was drawn (hidden, or the surface is too small). */
    int iptv_osd_draw(const iptv_osd_surface_t *surface, const iptv_osd_state_t *state,
                      uint32_t *first_row, uint32_t *rows);

    /* How many rows at the bottom of the surface's visible part the controls cover, so
     * subtitles can sit above them; 0 when hidden. */
    uint32_t iptv_osd_reserved_rows(const iptv_osd_surface_t *surface,
                                    const iptv_osd_state_t *state);
    /* How many columns at the right the settings menu covers; 0 when it is closed. */
    uint32_t iptv_osd_reserved_columns(const iptv_osd_surface_t *surface,
                                       const iptv_osd_state_t *state);

/* The settings menu can be painted by someone else (the interface, with its own components and
 * fonts): the painter draws the menu of `state` into `image`, premultiplied RGBA of
 * IPTV_OSD_MENU_IMAGE_WIDTH x IPTV_OSD_OVERLAY_HEIGHT that covers the right of the overlay, and
 * reports the rectangle it drew in. It runs on the thread that publishes the state, never on
 * the presenter's, and returns 0, or -1 to leave the menu to the built-in drawing. */
#define IPTV_OSD_MENU_IMAGE_WIDTH 800u

    typedef struct iptv_osd_image
    {
        uint8_t *rgba; /* IPTV_OSD_MENU_IMAGE_WIDTH * 4 bytes a row */
        uint32_t x;    /* what was drawn, in the image */
        uint32_t y;
        uint32_t width;
        uint32_t height;
    } iptv_osd_image_t;

    typedef int (*iptv_osd_menu_painter_t)(void *context, const iptv_osd_state_t *state,
                                           iptv_osd_image_t *image);

    /* Installs the painter (null removes it). Call it while nothing is playing. */
    void iptv_osd_set_menu_painter(iptv_osd_menu_painter_t painter, void *context);

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
