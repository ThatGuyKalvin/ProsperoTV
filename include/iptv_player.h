/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_PLAYER_H
#define IPTV_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runs one foreground channel session after the launcher has released
 * RmlUi, SDL, and VideoOut. Returns only after every native stream resource
 * has been released so the launcher can safely be recreated. */
int iptv_player_run(const char *url, const char *channel_name);
int iptv_player_run_with_headers(const char *url, const char *channel_name,
                                 const char *user_agent, const char *referrer,
                                 int reconnect_live);
const char *iptv_player_last_error(void);

/* A session with what its on-screen controls show. A film or episode (vod) can start at a
 * saved position; a live channel shows what is on now and next. */
typedef struct iptv_player_options
{
    const char *url;
    const char *channel_name;
    const char *user_agent;
    const char *referrer;
    int reconnect_live;
    int vod;
    long long start_position_us;
    const char *subtitle;  /* the episode, under the series title */
    const char *info_now;  /* live: the programme on now */
    const char *info_next; /* live: what follows, e.g. "Next  18:30  News" */
    long long info_start_unix; /* live: when the programme on now started and ends */
    long long info_end_unix;
} iptv_player_options_t;
int iptv_player_run_options(const iptv_player_options_t *options);
/* Where the last film or episode stopped and how long it is; -1 when unknown. A file played
 * to its end reports its duration as the position. */
long long iptv_player_last_position_us(void);
long long iptv_player_last_duration_us(void);
/* Runs the same foreground path with a bounded automatic stop. A zero timeout
 * disables the deadline. This is used by controlled hardware acceptance. */
int iptv_player_run_controlled(const char *url, const char *channel_name,
                               unsigned stop_after_ms);

#ifdef __cplusplus
}
#endif

#endif
