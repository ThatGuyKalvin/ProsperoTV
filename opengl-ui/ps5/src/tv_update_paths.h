/* ProsperoTV - The self-update kit's three paths.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * update_kit/self_update_ps5.c is written for an app in its sandbox (/app0,
 * /download0). ProsperoTV's folder and data are elsewhere once it has
 * filesystem access, so the build puts this header in front of that file
 * (ps5/patch_tree.py): 0 the helper in the app's folder, 1 the app's
 * param.json, 2 the file that keeps the catalog's highest sequence. */

#ifndef TV_UPDATE_PATHS_H
#define TV_UPDATE_PATHS_H

#ifdef __cplusplus
extern "C"
#endif
const char *tv_self_update_path(int which);

#define SELF_UPDATE_HELPER_PATH tv_self_update_path(0)
#define SELF_UPDATE_PARAM_PATH tv_self_update_path(1)
#define SELF_UPDATE_SEQUENCE_PATH tv_self_update_path(2)

#endif /* TV_UPDATE_PATHS_H */
