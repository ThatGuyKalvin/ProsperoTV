# ProsperoTV's next interface

ProsperoTV with a new interface built on the
[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) kit:
frosted panels over a red sky, a hero for the channel in focus, a scrolling
grid with the alphabet beside it, a search drawer, a tuning screen that runs
until a channel's first picture, and a notice when a newer version is listed
on homebrew.page. It is drawn with OpenGL (ps5-opengl SDK) instead of the
released app's SDL and RmlUi; the catalog, the stores, the network code and
the player are the released app's own sources, used from the folder above.

**It is not the released app yet.** The repository's root still builds and
releases ProsperoTV as before; nothing in this folder is built by its
`Makefile` or its workflow. This folder is developed and tested here until it
replaces the root build.

It compiles against two source trees in place:

| Needs | Default path | Override |
| --- | --- | --- |
| `ps5-homebrew-ui` (renderer, themes, components, font files, sounds) | a checkout beside this repository | `KIT=` |
| ProsperoTV (catalog, stores, network, player) | the repository this folder is in | `TV=` |

```bash
tools/run-tests.sh                 # the logic and the interface, no OpenGL, under ASan + UBSan
tools/host-snapshots.sh            # build for the PC, run the walk, write build/snapshots/*.png
HOST_SANITIZE=1 tools/host-snapshots.sh build/snapshots-sanitize
ps5/assemble.sh                    # make the console build tree beside the repository
make -C ../../prosperotv-ui-build  # dist/PPSA99003 (folder and zip)
TV_TEST_TITLE=PPSA88021 ps5/assemble.sh   # the same as a disposable title beside the released app
```

The test title also reads scripted runs (`ps5/src/tv_dev.hpp`), which
`tools/console-run.py <console address> <app folder> <results> <script>`
drives; the scripts are in `ps5/scripts/`.

## What is here

```
src/tv/               the interface and its logic (namespace ptv), as the console builds it
  model.*             sources, catalog, groups, search, favorites, playback requests: no drawing
  channel_text.*      what a record says: display name, picture size, monogram
  platform.hpp        what the logic asks of the machine (threads, clock, network)
  settings.*          reduce motion, sounds, menu sharpness
  theme.*             Dusk as kit tokens, and the sky behind everything
  draw.*              channel artwork, tiles, chips, the mark, names the fonts can write
  browse_screen.*     Live TV and Favorites: hero, list chips, grid
  search_sheet.*      the search and filter drawer
  sources_screen.*    the three sources and their state
  app.*               tabs, status, the Settings and About pages, hints, the failure dialog, notices
host/                 PC renderer, the scripted walk, stand-ins for keyboard and network
tests/                GoogleTest: the logic, the interface under scripted and random input
kit_patches/          ui::GridView with a cell count instead of items (from the Radio prototype)
ps5/                  the console entry point, runtime pieces, and the build-tree assembly
tools/                the builds, the font bake, the sample playlist
```

## How the console build is put together

`ps5/assemble.sh` writes a build tree outside both repositories:

- the **kit's build recipe** (its `tooling/`, `tools/build.sh`, linker script and
  module writer, the wrapped heap), which every OpenGL title of ours uses;
- **ProsperoTV's sources** except the RmlUi interface: `iptv_app.cpp`, the old
  `main.cpp`, the bitmap font engine and `ui/` are left out, and SDL, RmlUi and
  FreeType are no longer linked;
- the **kit files** listed in `ps5/kit-files.txt` under `src/kit/`;
- this folder's `src/tv/` and `ps5/src/` (the entry point, `tv_platform.cpp`,
  the heap and the runtime shims);
- fonts baked by `tools/bake-fonts.sh` and the kit's `glass` sound set.

`ps5/patch_tree.py` then makes the few edits the copies need (the keyboard
without SDL, the import libraries the public SDK lacks, the audio decoders) and
fails if any of the text it replaces has changed.

`ps5/src/main.cpp` alternates the two owners of the display: it opens the menu
(EGL display, renderer, controller, sounds), runs it until a channel is chosen,
closes all of it, plays the channel with ProsperoTV's own player, and opens the
menu again where it was. The catalog, the filters and the focus live in
`ptv::Model`, which outlives every menu session.

## Fonts

The kit bakes printable ASCII. A channel list needs more, so
`tools/bake-fonts.sh` bakes the kit's own font files (Inter, Montserrat,
DejaVu Sans Mono) with Latin-1, Latin Extended-A, Greek and Cyrillic. Names in
other scripts fall back to what the fonts can write, then to the playlist's
id for the channel (`shown_name` in `draw.cpp`). Faces for other scripts would
have to be added to the kit's `third_party/fonts`.

## What has run on a console

On two consoles with system software 6.02: the menu at 3840 x 2160 and 60
frames a second, the letters, pages on a held trigger, every tab, the
hand-over between the OpenGL menu and the player (the decoder's system module
is loaded before OpenGL starts, and the player accepts that AGC is already
initialised), 1080p H.264 channels with sound, the tuning screen, the
newer-version notice, and the app closing itself after a scripted run.

Not verified: a 4K channel (the ones tried were refused by their providers),
the system keyboard under a script, and how the home screen picks up the new
artwork.

A picture made on a PC shows what the code draws, not the frame rate, the
memory use or the sound of a console.
