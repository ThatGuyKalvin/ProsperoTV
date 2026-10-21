/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 ThatGuyKalvin
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_TV_REMOTE_H
#define IPTV_TV_REMOTE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The TV's remote over HDMI-CEC (HDMI Device Link): the system user's remote-control pad port. */
#define IPTV_TV_REMOTE_USER 0xFF
#define IPTV_TV_REMOTE_PORT 16
#define IPTV_TV_REMOTE_SAMPLE_SIZE 120U
/* Key code, 0 when none; arrows come as D-pad bits. Only 13 and 15 are confirmed on hardware. */
#define IPTV_TV_REMOTE_KEY_OFFSET 0x6CU

#define IPTV_TV_REMOTE_OPTIONS UINT32_C(0x00000008)
#define IPTV_TV_REMOTE_L1 UINT32_C(0x00000400)
#define IPTV_TV_REMOTE_R1 UINT32_C(0x00000800)
#define IPTV_TV_REMOTE_CIRCLE UINT32_C(0x00002000)
#define IPTV_TV_REMOTE_CROSS UINT32_C(0x00004000)
#define IPTV_TV_REMOTE_PLAY UINT32_C(0x01000000)
#define IPTV_TV_REMOTE_REWIND UINT32_C(0x02000000)
#define IPTV_TV_REMOTE_FAST_FORWARD UINT32_C(0x04000000)
#define IPTV_TV_REMOTE_MEDIA_KEYS \
    (IPTV_TV_REMOTE_PLAY | IPTV_TV_REMOTE_REWIND | IPTV_TV_REMOTE_FAST_FORWARD)
#define IPTV_TV_REMOTE_PAD_BUTTONS UINT32_C(0x0010FFFE)
#define IPTV_TV_REMOTE_INTERCEPTED UINT32_C(0x80000000)

/* Remote samples carry the intercepted bit even while the app has the screen. */
static inline uint32_t iptv_tv_remote_buttons(const unsigned char *sample, bool system_has_screen)
{
    uint32_t raw;
    memcpy(&raw, sample, sizeof(raw));
    uint32_t buttons = raw & IPTV_TV_REMOTE_PAD_BUTTONS;
    switch (sample[IPTV_TV_REMOTE_KEY_OFFSET])
    {
    case 13: /* OK */
        buttons |= IPTV_TV_REMOTE_CROSS;
        break;
    case 15: /* Back */
        buttons |= IPTV_TV_REMOTE_CIRCLE;
        break;
    case 18: /* Menu */
    case 27: /* context menu */
        buttons |= IPTV_TV_REMOTE_OPTIONS;
        break;
    case 22: /* Play */
    case 26: /* Pause */
    case 45: /* Play/Pause */
        buttons |= IPTV_TV_REMOTE_PLAY;
        break;
    case 23:
        buttons |= IPTV_TV_REMOTE_REWIND;
        break;
    case 24:
        buttons |= IPTV_TV_REMOTE_FAST_FORWARD;
        break;
    case 41: /* Channel + */
        buttons |= IPTV_TV_REMOTE_R1;
        break;
    case 42: /* Channel - */
        buttons |= IPTV_TV_REMOTE_L1;
        break;
    default:
        break;
    }
    if ((raw & IPTV_TV_REMOTE_INTERCEPTED) != 0 && system_has_screen)
        return 0;
    return buttons;
}

/* Needs scePadInit. Keys already down when it opens count only once let go. */
int iptv_tv_remote_open(void);
int iptv_tv_remote_read(uint32_t *buttons, int capacity);
void iptv_tv_remote_close(void);

#ifdef __cplusplus
}
#endif

#endif
