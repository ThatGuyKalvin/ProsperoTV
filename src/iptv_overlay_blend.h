/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_OVERLAY_BLEND_H
#define IPTV_OVERLAY_BLEND_H

#include "iptv_osd.h"

#include <stddef.h>
#include <stdint.h>

/* Drawing into the screen overlay (see iptv_osd.h). Values are 10-bit; 8-bit surfaces keep the
 * top eight bits. The colour plane holds premultiplied colour measured from black (luma 64,
 * chroma 512), and the mask plane's luma holds the coverage, so "over" compositing is linear in
 * both. The mask's chroma stays neutral. */

static inline int iptv_overlay_get(const uint8_t *plane, uint32_t component_bytes, size_t index)
{
    return component_bytes == 2u ? ((const uint16_t *)plane)[index] : (int)plane[index] << 2;
}

static inline void iptv_overlay_set(uint8_t *plane, uint32_t component_bytes, size_t index,
                                    int value)
{
    if (value < 0)
        value = 0;
    if (value > 1023)
        value = 1023;
    if (component_bytes == 2u)
        ((uint16_t *)plane)[index] = (uint16_t)value;
    else
        plane[index] = (uint8_t)(value >= 1022 ? 255 : (value + 2) >> 2);
}

/* Puts `value` (a luma level) over the overlay at luma sample `index`, `alpha` out of 256. */
static inline void iptv_overlay_luma(const iptv_osd_surface_t *surface, size_t index, int value,
                                     int alpha)
{
    if (alpha <= 0)
        return;
    if (alpha > 256)
        alpha = 256;
    const uint32_t bytes = surface->component_bytes;
    const int colour = iptv_overlay_get(surface->data, bytes, index) - 64;
    const int cover = iptv_overlay_get(surface->mask, bytes, index) - 64;
    iptv_overlay_set(surface->data, bytes, index,
                     64 + (alpha * (value - 64) + (256 - alpha) * colour) / 256);
    iptv_overlay_set(surface->mask, bytes, index, 64 + (alpha * 876 + (256 - alpha) * cover) / 256);
}

/* The same for the chroma pair at `index` (U, then V). */
static inline void iptv_overlay_chroma(const iptv_osd_surface_t *surface, size_t index, int u,
                                       int v, int alpha)
{
    if (alpha <= 0)
        return;
    if (alpha > 256)
        alpha = 256;
    const uint32_t bytes = surface->component_bytes;
    const int old_u = iptv_overlay_get(surface->data, bytes, index) - 512;
    const int old_v = iptv_overlay_get(surface->data, bytes, index + 1u) - 512;
    iptv_overlay_set(surface->data, bytes, index,
                     512 + (alpha * (u - 512) + (256 - alpha) * old_u) / 256);
    iptv_overlay_set(surface->data, bytes, index + 1u,
                     512 + (alpha * (v - 512) + (256 - alpha) * old_v) / 256);
}

#endif
