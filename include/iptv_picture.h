/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_PICTURE_H
#define IPTV_PICTURE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Where the picture goes on the screen: its shape (aspect ratio) and how much of the screen it
     * covers (zoom). The player sets the modes; the presenter places each frame with
     * iptv_picture_layout, and the controls and subtitles stay inside the part that is visible. */

    enum
    {
        IPTV_ASPECT_AUTO = 0, /* the frame's own shape, including non-square pixels */
        IPTV_ASPECT_16_9,
        IPTV_ASPECT_4_3,
        IPTV_ASPECT_1_85,
        IPTV_ASPECT_2_39,
        IPTV_ASPECT_STRETCH, /* the whole screen, whatever the shape */
        IPTV_ASPECT_COUNT
    };

    enum
    {
        IPTV_ZOOM_FIT = 0, /* all of the picture, with bars where the shapes differ */
        IPTV_ZOOM_115,
        IPTV_ZOOM_133,
        IPTV_ZOOM_FILL, /* the whole screen, cutting off what does not fit */
        IPTV_ZOOM_COUNT
    };

    typedef struct iptv_picture_layout
    {
        /* On the screen: may extend past it when zoomed. */
        int32_t x;
        int32_t y;
        uint32_t width;
        uint32_t height;
        /* In the frame: the part that is on screen (even offsets and sizes). */
        uint32_t visible_x;
        uint32_t visible_y;
        uint32_t visible_width;
        uint32_t visible_height;
    } iptv_picture_layout_t;

    /* Places a frame of frame_width x frame_height with sample (pixel) aspect sar_num:sar_den
     * (0:0 or 0:x for square) on a screen_width x screen_height screen. Returns 0, or -1 for
     * empty sizes. */
    int iptv_picture_layout(uint32_t frame_width, uint32_t frame_height, uint32_t sar_num,
                            uint32_t sar_den, int aspect, int zoom, uint32_t screen_width,
                            uint32_t screen_height, iptv_picture_layout_t *layout);

    const char *iptv_picture_aspect_label(int aspect);
    const char *iptv_picture_zoom_label(int zoom);

    /* A shape's width to height in thousandths: the mode's own (0 for Auto and Stretch), or the
     * frame's with its pixel shape (0 when unknown). */
    uint32_t iptv_picture_aspect_milli(int aspect);
    uint32_t iptv_picture_frame_milli(uint32_t frame_width, uint32_t frame_height, uint32_t sar_num,
                                      uint32_t sar_den);
    /* What a shape is called: "16:9", "4:3", "1.85:1", "2.39:1" and the like when it is close to
     * one, else its ratio ("1.50:1"). Empty for 0. */
    void iptv_picture_shape_name(uint32_t milli, char *out, size_t capacity);

    /* The modes in use, shared between the player and the presenter. */
    void iptv_picture_set_modes(int aspect, int zoom);
    void iptv_picture_modes(int *aspect, int *zoom);
    /* The playing stream's pixel shape (0:0 when square or unknown). */
    void iptv_picture_set_sample_aspect(uint32_t num, uint32_t den);
    void iptv_picture_sample_aspect(uint32_t *num, uint32_t *den);
    /* The playing stream's picture size (0 x 0 when unknown). */
    void iptv_picture_set_frame_size(uint32_t width, uint32_t height);
    void iptv_picture_frame_size(uint32_t *width, uint32_t *height);

#ifdef __cplusplus
}
#endif

#endif
