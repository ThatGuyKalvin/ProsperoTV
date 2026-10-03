/* ProsperoTV - The tuning screen: from the menu's last picture to the channel's first.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The menu hands over a picture of its tuning screen (the bar still empty)
 * before it closes. The player's loading thread shows it as video, its bar
 * creeping on as the channel opens; the player's stages lift the bar's floor
 * and ceiling. When the first picture is decoded the bar runs to its end and
 * the screen fades out, and the channel takes its place.
 *
 * The player's loading thread calls tv_tuning_compose(); everything else is
 * called from the thread that plays the channel. */

#ifndef TV_TUNING_H
#define TV_TUNING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* rgba: 1920 x 1080 sRGB pixels, top row first. The bar, in those
     * pixels; fill is its colour; progress where it starts (0 to 1). */
    void tv_tuning_set_picture(const uint8_t *rgba, float bar_x, float bar_y, float bar_width,
                               float bar_height, const uint8_t fill[3], float progress);
    /* The channel has played (or failed): the picture goes. */
    void tv_tuning_clear(void);
    /* A picture is up, or about to be, and has not faded out yet. */
    int tv_tuning_active(void);
    /* The bar is at least at floor, and creeps towards ceiling. */
    void tv_tuning_stage(float floor, float ceiling);
    /* The loading thread has a new surface: the next frame writes all of it. */
    void tv_tuning_surface_changed(void);
    /* The first picture is ready: run the bar to its end, then fade out. */
    void tv_tuning_finishing(void);
    /* Writes the next frame into a 1920 x 1088 NV12 surface (pitch 1920).
     * Returns 0 once the screen has faded out (nothing written). */
    int tv_tuning_compose(void *surface, size_t surface_bytes);
    /* Test builds: where to leave pictures of the screen (NULL: nowhere). */
    void tv_tuning_set_dump_dir(const char *directory);

#ifdef __cplusplus
}
#endif

#endif
