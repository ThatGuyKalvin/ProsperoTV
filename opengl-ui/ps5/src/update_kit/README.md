# Update check and self-update

These files are the PS5 Native App Boilerplate's (`examples/update-check` and
`examples/self-update`, boilerplate commit 2966cac), copied unchanged, as
ProsperoEden uses them:

| File | What it is |
| --- | --- |
| `update_check.{h,c}` | reading `param.json`, comparing content versions, the catalog's answer |
| `console_curl.{h,c}` | what PacBrew's libcurl needs to run in a native title: name lookups on the system resolver, libc stand-ins, the `fcntl` wrapper, the console's certificate list |
| `self_update.{h,c}` | the signed-catalog check (Ed25519, sequence guard) and the update job |
| `self_update_ps5.c` | the console side: libcurl, OpenSSL, the payload loader on port 9021 |
| `self_update_sha256.{h,c}`, `self_update_protocol.h` | shared with the helper |

`self_update_ps5.c` takes the paths of the helper, of the app's `param.json`
and of the sequence file from `SELF_UPDATE_HELPER_PATH`,
`SELF_UPDATE_PARAM_PATH` and `SELF_UPDATE_SEQUENCE_PATH` when they are defined.
ProsperoTV defines them (`../tv_update_paths.h`, put in front of that file by
`ps5/patch_tree.py`) because its folder is not `/app0` once it runs with
filesystem access. `../tv_update.cpp` is ProsperoTV's use of the kit, and
`src/tv/update_sheet.*` what the viewer sees of it.

The helper the app sends to the console's payload loader is in
`ps5/update_helper` (the boilerplate's `examples/self-update-helper` with
ProsperoEden's two changes: only the entries a release replaces are moved
aside, so anything else in the app's folder stays; and the installed folder
is taken from ShadowMountPlus's `/user/app/<TITLEID>/mount.lnk` before the
usual folders are scanned, with an image install refused). It unpacks with
miniz 3.0.2 (`ps5/third_party/miniz`, MIT).

How an update goes:

1. Once per launch, on a thread of its own, the app fetches the catalog's
   signed manifest and its own entry from homebrew.page, verifies the
   signature and the hashes, and compares the listed content version with
   `sce_sys/param.json`.
2. A newer release is offered. Nothing happens unless the viewer says so.
3. The app downloads the release ZIP from GitHub over HTTPS and streams it to
   the helper, which checks its SHA-256 against the signed catalog and unpacks
   it beside the app.
4. The app gives the go-ahead and closes itself. The helper replaces the
   app's files, refreshes the home screen's copies and posts a notification.

A cancel or a failure before step 4 leaves the installed app untouched.

Test titles also read `dev/update-offer.txt` beside the app (five lines: the
newer content version, the release's name, its ZIP on GitHub, the ZIP's
SHA-256, its size), which replaces the catalog's answer so an update can be
tried before the catalog lists one, and `dev/update-as.txt`
(`PPSA99003 01.000.000`) to ask the catalog as another title and version.

As in ProsperoEden, `self_update_check` also copies the catalog entry's
`release_notes` and `release_notes_truncated` into the offer (`notes`,
`notes_truncated`), for the update dialog's What's new view; the lines marked
"ProsperoEden:" in `self_update.c` and `self_update.h` are that change. In
`dev/update-offer.txt`, any lines after the fifth are the release notes.
