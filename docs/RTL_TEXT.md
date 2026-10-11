# Arabic and Hebrew rendering

Settings → Interface language selects the system language or one of the nine
app languages. Restart applies the saved choice. Arabic uses documented console
language ID 21; Hebrew is an app override, not an invented console language ID.

The pinned kit patch adds scoped logical RTL coordinates. Each app draw scope
restores its prior direction. Images and video keep their original UVs; text
blocks move without reversing their shaped glyphs. Menu left/right inputs follow
the reflected controls. Player seeking remains left=back/right=forward; only
its track-menu columns change navigation direction.

Primary font bundles contain the original face and a licensed DejaVu Sans
Arabic/Hebrew subset with OpenType tables. HarfBuzz shapes glyph IDs; FriBidi
orders runs from paragraph content, independently of the interface language.
Provider names remain unchanged in the catalog and search. Existing CJK faces
remain separate. Font loading validates sizes and glyph IDs; shaping caps each
operation at 16 KiB / 4096 codepoints. Per-face caches hold at most 64 entries,
64 KiB of text and 32768 positioned glyphs, protected by a mutex. Buffers are
local to each shaping operation. Joined Arabic ignores heading letter spacing.

`tools/setup-text-dependencies.sh` pins host dependencies and font checksums.
The native build compiles the same HarfBuzz source with upstream HB_NO_SETLOCALE,
using the pinned SDK and the existing PacBrew FriBidi archive. This avoids
POSIX locale-object imports unavailable in the console runtime.
`tools/fetch-kit.sh` applies the persistent patch to its own pinned checkout;
a custom KIT must already contain that patch. Font/dependency outputs remain
inside the selected worktree. Font and library licenses accompany the bundles.

Validation entry points:

- `opengl-ui/tools/check-font-shaping.sh`: real baked fonts, ASan/UBSan,
  Arabic ligatures, Hebrew points, bidi runs, wrap/ellipsis bounds, malformed
  input, concurrent caches, larger text, nested reflected clips and unchanged
  image/text UVs, URLs, IP addresses and certificate fingerprints.
- `opengl-ui/tools/run-tests.sh`: language catalog and complete app tests.
- `TV_LANGUAGE_OVERRIDE=8 opengl-ui/tools/host-snapshots.sh <output>`: Arabic
  full walk; choice 9 selects Hebrew. `TV_SHARING_REVIEW=1` limits the walk to
  sharing dialogs, including certificate/IP text.

Host evidence: 173 standalone checks and five focused app/catalog tests pass.
Arabic Live TV and guide, and Hebrew sharing dialogs rendered correctly under
software OpenGL with no GL errors. Full host suite and native linking are
tracked separately; physical console acceptance has not been performed.
