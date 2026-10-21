/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_input.h"
#include "iptv_remote.h"
#include "iptv_tv_remote.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define INPUT_QUEUE_SIZE 64U
#define PAD_SAMPLE_SIZE 120U
#define PAD_SAMPLE_CAPACITY 64
#define PAD_BUTTON_INTERCEPTED UINT32_C(0x80000000)
#define STICK_LOW (128U - IPTV_INPUT_STICK_DEADZONE)
#define STICK_HIGH (128U + IPTV_INPUT_STICK_DEADZONE)

typedef struct
{
    uint32_t mask;
    iptv_input_action_t action;
} button_map_t;

/* The controller and the TV remote each keep their own: one letting go must
 * not release what the other holds. */
typedef struct
{
    uint32_t buttons;
    int direction;
    uint64_t repeat_at;
} device_state_t;

extern int scePadInit(void);
extern int scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void *param);
extern int scePadClose(int32_t handle);
extern int scePadRead(int32_t handle, void *data, int32_t num);
extern int sceUserServiceInitialize(void *init_params);
extern int sceUserServiceGetInitialUser(int32_t *user_id);
extern int sceUserServiceTerminate(void);

static const button_map_t buttons[] = {
    {UINT32_C(0x00004000), IPTV_INPUT_CROSS},     {UINT32_C(0x00002000), IPTV_INPUT_CIRCLE},
    {UINT32_C(0x00008000), IPTV_INPUT_SQUARE},    {UINT32_C(0x00001000), IPTV_INPUT_TRIANGLE},
    {UINT32_C(0x00000008), IPTV_INPUT_OPTIONS},   {UINT32_C(0x00000400), IPTV_INPUT_L1},
    {UINT32_C(0x00000800), IPTV_INPUT_R1},        {UINT32_C(0x00100000), IPTV_INPUT_TOUCHPAD},
    {UINT32_C(0x00000100), IPTV_INPUT_L2},        {UINT32_C(0x00000200), IPTV_INPUT_GO_LIVE},
    {IPTV_TV_REMOTE_PLAY, IPTV_INPUT_PLAY_PAUSE},
};

static iptv_input_event_t queue[INPUT_QUEUE_SIZE];
static unsigned char samples[PAD_SAMPLE_CAPACITY][PAD_SAMPLE_SIZE];
static unsigned queue_read;
static unsigned queue_write;
static device_state_t pad_state = {0, -1, 0};
static device_state_t tv_state = {0, -1, 0};
static int32_t pad_handle = -1;
static bool owns_user_service;

static uint64_t now_ms(void)
{
    struct timespec now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) + (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void queue_push(iptv_input_action_t action, bool pressed)
{
    const unsigned next = (queue_write + 1U) % INPUT_QUEUE_SIZE;
    if (next == queue_read)
    {
        /* ponytail: bounded input queue; discard the oldest event under load. */
        queue_read = (queue_read + 1U) % INPUT_QUEUE_SIZE;
    }
    queue[queue_write] = (iptv_input_event_t){action, pressed};
    queue_write = next;
}

static int stick_direction(uint8_t x, uint8_t y)
{
    const int horizontal = (int)x - 128;
    const int vertical = (int)y - 128;
    const int horizontal_size = horizontal < 0 ? -horizontal : horizontal;
    const int vertical_size = vertical < 0 ? -vertical : vertical;

    if (horizontal_size < (int)IPTV_INPUT_STICK_DEADZONE &&
        vertical_size < (int)IPTV_INPUT_STICK_DEADZONE)
        return -1;
    if (horizontal_size > vertical_size)
    {
        if (x < STICK_LOW)
            return IPTV_INPUT_LEFT;
        if (x > STICK_HIGH)
            return IPTV_INPUT_RIGHT;
    }
    else
    {
        if (y < STICK_LOW)
            return IPTV_INPUT_UP;
        if (y > STICK_HIGH)
            return IPTV_INPUT_DOWN;
    }
    return -1;
}

static int dpad_direction(uint32_t buttons_state)
{
    if ((buttons_state & UINT32_C(0x00000010)) != 0)
        return IPTV_INPUT_UP;
    if ((buttons_state & UINT32_C(0x00000040)) != 0)
        return IPTV_INPUT_DOWN;
    if ((buttons_state & UINT32_C(0x00000080)) != 0)
        return IPTV_INPUT_LEFT;
    if ((buttons_state & UINT32_C(0x00000020)) != 0)
        return IPTV_INPUT_RIGHT;
    return -1;
}

static void update_direction(device_state_t *device, int next_direction)
{
    if (next_direction == device->direction)
        return;
    if (device->direction >= 0)
        queue_push((iptv_input_action_t)device->direction, false);
    device->direction = next_direction;
    if (device->direction >= 0)
    {
        queue_push((iptv_input_action_t)device->direction, true);
        device->repeat_at = now_ms() + IPTV_INPUT_REPEAT_DELAY_MS;
    }
    else
    {
        device->repeat_at = 0;
    }
}

static void process_buttons(device_state_t *device, uint32_t current, int analog_direction)
{
    const uint32_t changed = device->buttons ^ current;
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        if ((changed & buttons[i].mask) != 0)
        {
            queue_push(buttons[i].action, (current & buttons[i].mask) != 0);
        }
    }
    device->buttons = current;

    const int digital_direction = dpad_direction(current);
    update_direction(device, digital_direction >= 0 ? digital_direction : analog_direction);
}

static void process_sample(const unsigned char *sample)
{
    uint32_t current;
    memcpy(&current, sample, sizeof(current));
    const bool neutral = sample[76] == 0 || (current & PAD_BUTTON_INTERCEPTED) != 0;
    if (neutral)
        current = 0;
    current &= ~(uint32_t)IPTV_TV_REMOTE_MEDIA_KEYS;
    process_buttons(&pad_state, current, neutral ? -1 : stick_direction(sample[4], sample[5]));
}

/* Rewind and Fast forward step like Left and Right. */
static uint32_t tv_remote_pad_bits(uint32_t word)
{
    uint32_t current = word & ~(uint32_t)(IPTV_TV_REMOTE_REWIND | IPTV_TV_REMOTE_FAST_FORWARD);
    if ((word & IPTV_TV_REMOTE_REWIND) != 0)
        current |= UINT32_C(0x00000080);
    if ((word & IPTV_TV_REMOTE_FAST_FORWARD) != 0)
        current |= UINT32_C(0x00000020);
    return current;
}

bool iptv_input_init(void)
{
    queue_read = queue_write = 0;
    pad_state = tv_state = (device_state_t){0, -1, 0};
    pad_handle = -1;
    owns_user_service = false;

    const int user_init = sceUserServiceInitialize(NULL);
    owns_user_service = user_init == 0;

    int32_t user_id = -1;
    if (sceUserServiceGetInitialUser(&user_id) < 0 || scePadInit() < 0)
    {
        iptv_input_shutdown();
        return false;
    }

    pad_handle = scePadOpen(user_id, 0, 0, NULL);
    if (pad_handle < 0)
    {
        iptv_input_shutdown();
        return false;
    }
    /* Without HDMI Device Link there is no such port; the controller is enough. */
    (void)iptv_tv_remote_open();
    return true;
}

#define SCHEDULE_CAPACITY 64U
static struct
{
    uint64_t at_ms;
    iptv_input_action_t action;
    bool waiting;
} scheduled[SCHEDULE_CAPACITY];
static uint64_t schedule_started_ms;
static bool schedule_running;

bool iptv_input_schedule(unsigned delay_ms, iptv_input_action_t action)
{
    if (action < 0 || action >= IPTV_INPUT_COUNT)
        return false;
    for (unsigned i = 0; i < SCHEDULE_CAPACITY; ++i)
    {
        if (scheduled[i].waiting)
            continue;
        scheduled[i].at_ms = delay_ms;
        scheduled[i].action = action;
        scheduled[i].waiting = true;
        return true;
    }
    return false;
}

void iptv_input_schedule_start(void)
{
    if (schedule_running)
        return;
    for (unsigned i = 0; i < SCHEDULE_CAPACITY; ++i)
    {
        if (scheduled[i].waiting)
        {
            schedule_started_ms = now_ms();
            schedule_running = true;
            return;
        }
    }
}

static void schedule_poll(void)
{
    if (!schedule_running)
        return;
    const uint64_t elapsed = now_ms() - schedule_started_ms;
    bool left = false;
    for (unsigned i = 0; i < SCHEDULE_CAPACITY; ++i)
    {
        if (!scheduled[i].waiting)
            continue;
        if (scheduled[i].at_ms > elapsed)
        {
            left = true;
            continue;
        }
        scheduled[i].waiting = false;
        queue_push(scheduled[i].action, true);
        queue_push(scheduled[i].action, false);
    }
    schedule_running = left;
}

void iptv_input_poll(void)
{
    iptv_remote_poll();
    schedule_poll();
    if (pad_handle < 0)
        return;

    /* scePadRead is the PSRadio non-blocking sample path; never wait here. */
    int count = scePadRead(pad_handle, samples, PAD_SAMPLE_CAPACITY);
    if (count < 0)
        count = 0;
    if (count > PAD_SAMPLE_CAPACITY)
        count = PAD_SAMPLE_CAPACITY;
    for (int i = 0; i < count; ++i)
        process_sample(samples[i]);

    uint32_t tv_words[PAD_SAMPLE_CAPACITY];
    const int tv_count = iptv_tv_remote_read(tv_words, PAD_SAMPLE_CAPACITY);
    for (int i = 0; i < tv_count; ++i)
        process_buttons(&tv_state, tv_remote_pad_bits(tv_words[i]), -1);

    device_state_t *const devices[] = {&pad_state, &tv_state};
    const uint64_t now = now_ms();
    for (unsigned i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i)
    {
        if (devices[i]->direction >= 0 && now >= devices[i]->repeat_at)
        {
            queue_push((iptv_input_action_t)devices[i]->direction, true);
            devices[i]->repeat_at = now + IPTV_INPUT_REPEAT_MS;
        }
    }
}

bool iptv_input_next(iptv_input_event_t *event)
{
    if (event == NULL)
        return false;
    if (queue_read == queue_write)
        return iptv_remote_next(event);
    *event = queue[queue_read];
    queue_read = (queue_read + 1U) % INPUT_QUEUE_SIZE;
    return true;
}

bool iptv_input_pressed(iptv_input_action_t action)
{
    if (action < 0 || action >= IPTV_INPUT_COUNT)
        return false;
    if (action >= IPTV_INPUT_UP && action <= IPTV_INPUT_RIGHT)
        return pad_state.direction == (int)action || tv_state.direction == (int)action;
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
    {
        if (buttons[i].action == action)
            return ((pad_state.buttons | tv_state.buttons) & buttons[i].mask) != 0;
    }
    return false;
}

void iptv_input_shutdown(void)
{
    iptv_tv_remote_close();
    if (pad_handle >= 0)
    {
        scePadClose(pad_handle);
        pad_handle = -1;
    }
    if (owns_user_service)
    {
        sceUserServiceTerminate();
        owns_user_service = false;
    }
    queue_read = queue_write = 0;
    pad_state = tv_state = (device_state_t){0, -1, 0};
}
