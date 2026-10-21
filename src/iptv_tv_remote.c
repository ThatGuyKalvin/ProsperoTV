/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 ThatGuyKalvin
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_tv_remote.h"

#include <stddef.h>

#define REMOTE_SAMPLE_CAPACITY 64

/* sceSystemServiceGetStatus's answer: the PS4 layout, with room to spare. */
typedef struct
{
    int32_t event_count;
    uint8_t system_ui_overlaid;
    uint8_t in_background;
    uint8_t cpu_mode7_normal;
    uint8_t live_streaming;
    uint8_t out_of_vr_area;
    uint8_t reserved[251];
} system_status_t;

extern int scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void *param);
extern int scePadClose(int32_t handle);
extern int scePadRead(int32_t handle, void *data, int32_t num);
extern int scePadReadState(int32_t handle, void *data);
extern int sceSystemServiceGetStatus(system_status_t *status);

static unsigned char samples[REMOTE_SAMPLE_CAPACITY][IPTV_TV_REMOTE_SAMPLE_SIZE];
static int32_t remote_handle = -1;
static uint32_t ignored;

static bool system_has_screen(void)
{
    system_status_t status;
    memset(&status, 0, sizeof(status));
    return sceSystemServiceGetStatus(&status) != 0 || status.system_ui_overlaid != 0 ||
           status.in_background != 0;
}

int iptv_tv_remote_open(void)
{
    if (remote_handle >= 0)
        return remote_handle;
    const int handle = scePadOpen(IPTV_TV_REMOTE_USER, IPTV_TV_REMOTE_PORT, 0, NULL);
    if (handle < 0)
        return handle;
    remote_handle = handle;
    /* Opening is the one time the cached state is read: no batch read races it. */
    ignored = 0;
    if (scePadReadState(remote_handle, samples[0]) == 0)
        ignored = iptv_tv_remote_buttons(samples[0], false);
    return remote_handle;
}

int iptv_tv_remote_read(uint32_t *buttons, int capacity)
{
    if (remote_handle < 0 || buttons == NULL || capacity <= 0)
        return 0;
    if (capacity > REMOTE_SAMPLE_CAPACITY)
        capacity = REMOTE_SAMPLE_CAPACITY;
    int count = scePadRead(remote_handle, samples, capacity);
    if (count <= 0)
        return 0;
    if (count > capacity)
        count = capacity;
    int screen = -1;
    for (int i = 0; i < count; ++i)
    {
        uint32_t raw;
        memcpy(&raw, samples[i], sizeof(raw));
        if ((raw & IPTV_TV_REMOTE_INTERCEPTED) != 0 && screen < 0)
            screen = system_has_screen() ? 1 : 0;
        uint32_t word = iptv_tv_remote_buttons(samples[i], screen > 0);
        ignored &= word;
        buttons[i] = word & ~ignored;
    }
    return count;
}

void iptv_tv_remote_close(void)
{
    if (remote_handle >= 0)
        scePadClose(remote_handle);
    remote_handle = -1;
    ignored = 0;
}
