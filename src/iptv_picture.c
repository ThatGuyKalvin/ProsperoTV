/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_picture.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>

static double aspect_value(int aspect, uint32_t frame_width, uint32_t frame_height,
                           uint32_t sar_num, uint32_t sar_den, uint32_t screen_width,
                           uint32_t screen_height)
{
    switch (aspect)
    {
    case IPTV_ASPECT_16_9:
        return 16.0 / 9.0;
    case IPTV_ASPECT_4_3:
        return 4.0 / 3.0;
    case IPTV_ASPECT_1_85:
        return 1.85;
    case IPTV_ASPECT_2_39:
        return 2.39;
    case IPTV_ASPECT_STRETCH:
        return (double)screen_width / (double)screen_height;
    default:
    {
        double shape = (double)frame_width / (double)frame_height;
        /* Pixel shapes outside 1:4..4:1 are damaged streams, not pictures. */
        if (sar_num && sar_den && sar_num <= sar_den * 4u && sar_den <= sar_num * 4u)
            shape = shape * (double)sar_num / (double)sar_den;
        return shape;
    }
    }
}

static int32_t round_even(double value)
{
    const double rounded = value < 0 ? -(double)(int64_t)(-value * 0.5 + 0.5) * 2.0
                                     : (double)(int64_t)(value * 0.5 + 0.5) * 2.0;
    return (int32_t)rounded;
}

int iptv_picture_layout(uint32_t frame_width, uint32_t frame_height, uint32_t sar_num,
                        uint32_t sar_den, int aspect, int zoom, uint32_t screen_width,
                        uint32_t screen_height, iptv_picture_layout_t *layout)
{
    if (!layout || !frame_width || !frame_height || !screen_width || !screen_height)
        return -1;
    const double screen = (double)screen_width / (double)screen_height;
    const double shape = aspect_value(aspect, frame_width, frame_height, sar_num, sar_den,
                                      screen_width, screen_height);

    /* Fitted inside the screen first. */
    double width = (double)screen_width;
    double height = (double)screen_height;
    if (shape > screen)
        height = width / shape;
    else
        width = height * shape;
    double factor = 1.0;
    switch (zoom)
    {
    case IPTV_ZOOM_115:
        factor = 1.15;
        break;
    case IPTV_ZOOM_133:
        factor = 4.0 / 3.0;
        break;
    case IPTV_ZOOM_FILL:
        factor = shape > screen ? (double)screen_height / height : (double)screen_width / width;
        break;
    default:
        break;
    }
    width *= factor;
    height *= factor;

    int32_t out_width = round_even(width);
    int32_t out_height = round_even(height);
    if (out_width < 2)
        out_width = 2;
    if (out_height < 2)
        out_height = 2;
    layout->width = (uint32_t)out_width;
    layout->height = (uint32_t)out_height;
    layout->x = ((int32_t)screen_width - out_width) / 2;
    layout->y = ((int32_t)screen_height - out_height) / 2;

    /* The part of the frame on screen. */
    uint32_t visible_width = frame_width;
    uint32_t visible_height = frame_height;
    if (layout->width > screen_width)
        visible_width = (uint32_t)((double)frame_width * screen_width / layout->width);
    if (layout->height > screen_height)
        visible_height = (uint32_t)((double)frame_height * screen_height / layout->height);
    visible_width &= ~1u;
    visible_height &= ~1u;
    if (visible_width < 2u)
        visible_width = frame_width < 2u ? frame_width : 2u;
    if (visible_height < 2u)
        visible_height = frame_height < 2u ? frame_height : 2u;
    layout->visible_width = visible_width;
    layout->visible_height = visible_height;
    layout->visible_x = ((frame_width - visible_width) / 2u) & ~1u;
    layout->visible_y = ((frame_height - visible_height) / 2u) & ~1u;
    return 0;
}

const char *iptv_picture_aspect_label(int aspect)
{
    static const char *const labels[IPTV_ASPECT_COUNT] = {"Auto",   "16:9",   "4:3",
                                                          "1.85:1", "2.39:1", "Stretch"};
    return aspect >= 0 && aspect < IPTV_ASPECT_COUNT ? labels[aspect] : labels[0];
}

const char *iptv_picture_zoom_label(int zoom)
{
    static const char *const labels[IPTV_ZOOM_COUNT] = {"Fit", "115%", "133%", "Fill screen"};
    return zoom >= 0 && zoom < IPTV_ZOOM_COUNT ? labels[zoom] : labels[0];
}

uint32_t iptv_picture_aspect_milli(int aspect)
{
    switch (aspect)
    {
    case IPTV_ASPECT_16_9:
        return 1778u;
    case IPTV_ASPECT_4_3:
        return 1333u;
    case IPTV_ASPECT_1_85:
        return 1850u;
    case IPTV_ASPECT_2_39:
        return 2390u;
    default:
        return 0u;
    }
}

uint32_t iptv_picture_frame_milli(uint32_t frame_width, uint32_t frame_height, uint32_t sar_num,
                                  uint32_t sar_den)
{
    if (!frame_width || !frame_height)
        return 0u;
    const double shape =
        aspect_value(IPTV_ASPECT_AUTO, frame_width, frame_height, sar_num, sar_den, 1u, 1u);
    return shape > 0.0 && shape < 10.0 ? (uint32_t)(shape * 1000.0 + 0.5) : 0u;
}

void iptv_picture_shape_name(uint32_t milli, char *out, size_t capacity)
{
    static const struct
    {
        uint32_t milli;
        const char *name;
    } known[] = {{1250u, "5:4"},  {1333u, "4:3"},    {1500u, "3:2"},   {1667u, "5:3"},
                 {1778u, "16:9"}, {1850u, "1.85:1"}, {2000u, "2:1"},   {2200u, "2.20:1"},
                 {2333u, "21:9"}, {2390u, "2.39:1"}, {2760u, "2.76:1"}};
    if (!out || !capacity)
        return;
    out[0] = '\0';
    if (!milli)
        return;
    for (size_t index = 0; index < sizeof(known) / sizeof(known[0]); ++index)
    {
        /* Within 1.5 %: encoders round sizes to whole blocks. */
        const uint32_t target = known[index].milli;
        const uint32_t difference = milli > target ? milli - target : target - milli;
        if (difference * 1000u <= target * 15u)
        {
            snprintf(out, capacity, "%s", known[index].name);
            return;
        }
    }
    snprintf(out, capacity, "%u.%02u:1", milli / 1000u, (milli % 1000u + 5u) / 10u % 100u);
}

static _Atomic int picture_aspect;
static _Atomic int picture_zoom;
static _Atomic uint32_t picture_sar_num;
static _Atomic uint32_t picture_sar_den;
static _Atomic uint32_t picture_frame_width;
static _Atomic uint32_t picture_frame_height;

void iptv_picture_set_modes(int aspect, int zoom)
{
    atomic_store_explicit(&picture_aspect,
                          aspect >= 0 && aspect < IPTV_ASPECT_COUNT ? aspect : IPTV_ASPECT_AUTO,
                          memory_order_relaxed);
    atomic_store_explicit(&picture_zoom, zoom >= 0 && zoom < IPTV_ZOOM_COUNT ? zoom : IPTV_ZOOM_FIT,
                          memory_order_relaxed);
}

void iptv_picture_modes(int *aspect, int *zoom)
{
    if (aspect)
        *aspect = atomic_load_explicit(&picture_aspect, memory_order_relaxed);
    if (zoom)
        *zoom = atomic_load_explicit(&picture_zoom, memory_order_relaxed);
}

void iptv_picture_set_sample_aspect(uint32_t num, uint32_t den)
{
    atomic_store_explicit(&picture_sar_num, num, memory_order_relaxed);
    atomic_store_explicit(&picture_sar_den, den, memory_order_relaxed);
}

void iptv_picture_sample_aspect(uint32_t *num, uint32_t *den)
{
    if (num)
        *num = atomic_load_explicit(&picture_sar_num, memory_order_relaxed);
    if (den)
        *den = atomic_load_explicit(&picture_sar_den, memory_order_relaxed);
}

void iptv_picture_set_frame_size(uint32_t width, uint32_t height)
{
    atomic_store_explicit(&picture_frame_width, width, memory_order_relaxed);
    atomic_store_explicit(&picture_frame_height, height, memory_order_relaxed);
}

void iptv_picture_frame_size(uint32_t *width, uint32_t *height)
{
    if (width)
        *width = atomic_load_explicit(&picture_frame_width, memory_order_relaxed);
    if (height)
        *height = atomic_load_explicit(&picture_frame_height, memory_order_relaxed);
}
