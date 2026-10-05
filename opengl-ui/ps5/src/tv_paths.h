/* ProsperoTV - Where a file of the app is, by its name.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The app's files are in /data/prosperotv when it has filesystem access and
 * in the title's own storage (/download0) when it has not, and its own folder
 * is /app0 only inside the sandbox. Which of the two holds is known once
 * tv::storage::initialize() has run, first thing in main; these give the path
 * to code that is written against a fixed one (ps5/patch_tree.py points the
 * player's and the stores' paths here).
 *
 * Both return the same pointer for the same name for the life of the process.
 * The text behind it is the sandbox path until initialize() has run and the
 * settled path afterwards, so a constant initialised before main is right by
 * the time anything opens it. */

#ifndef TV_PATHS_H
#define TV_PATHS_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* A file the app keeps ("iptv-favorites-v1.bin"): settings and lists in
     * config/, downloaded channel lists in cache/, receipts in logs/. */
    const char *tv_data_file(const char *name);
    /* A file of the app's own folder ("assets/fonts/inter-regular.huifont"). */
    const char *tv_app_file(const char *relative);

#ifdef __cplusplus
}
#endif

#endif /* TV_PATHS_H */
