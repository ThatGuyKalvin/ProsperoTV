# Provider reports and remaining interface work

## VOD seek feedback

- G1: retain the completed video output across same-video seek/speed decoder
  reopens; release it on every exit. Test output retention/cleanup, timeline
  progress, buffering feedback and persistent non-default speed display.
- G2: on idle .30, use a disposable title and synthetic ranged MP4 to seek
  forward/back, change speed and pause/resume. Require retained-frame and
  resumed-frame diagnostics, successful playback and clean teardown.

The held picture shows a translated Buffering message until new frames arrive.
Triangle reveals elapsed/total time and progress; a non-default speed remains
visible after the full controls hide. [PS5 VLC](https://github.com/ZiZc3/VLC)
was reviewed for its seek and rate controls; no VLC code was copied.

- 2026-10-11 | G1 | b18084b | host | pass: 33 tooling/native checks, 9 focused HUD/UI sanitizer tests, lint and native static analysis; PS5 build passed.
- 2026-10-11 | G2 | b18084b | .40/PPSA88308 | pass: case97, five held/resumed frames, seek/speed through 2x, pause/resume, cleanup0; results/integration/console-97-40.

The fixture was a ranged H.264/AAC MP4. HDR format changes, other containers
and provider-specific network behavior were not exercised in this case.

Baseline: `550a20b` (11 October 2026). Reported provider responses have not
been supplied, so fixture compatibility checks must not be described as
reproductions of those accounts.

- G1: #33 / #36 — accept valid Xtream authentication, category, live and
  on-demand responses, preserve malformed-input rejection and parental flags.
- G2: #34 / #36 — import extended M3U/M3U Plus, keep live channels in Live TV,
  route movies and series to On Demand, preserve metadata across reload, and
  play selected titles with their request headers.
- G3: #35 — preserve picture aspect ratio and expose correction controls;
  verify renderer output and controller operation.
- G4: Watching — USB recording and guide scheduling; resume/next episode;
  seeking/progress/chapters; playback speed; zoom/fill/subtitle size and colour;
  picture in picture; sound delay; programme reminders.
- G5: Browsing — provider channel numbers/keypad; reorder/rename/hide channels
  and favorites; unified search; movie/show metadata and genre/year filters;
  recently added/most watched; home screen.
- G6: The app — themes, idle/paused screensaver, larger text/high contrast,
  and spoken focus if platform text-to-speech is usable.

Each change requires focused host regression checks and a committed candidate
before a bounded native case on idle console `.30`. Preserve other running
titles, acquire the shared environment lock, use a disposable development
title, verify deployed hashes, and check clean teardown. Native cases use the
owner-started services without changing console settings or access privileges.
Record unverified physical behavior separately. Keep private accounts, fixture
media, raw captures and receipts outside Git. Open reviewable feature PRs as
work completes; remaining roadmap items are not complete merely because G1–G3
pass.

After G1-G6 are complete and verified, continue with remaining backlog items,
starting with the easiest. Recheck console availability before every native
case; another running title always takes priority.

- 2026-10-11 | G5 | channel organization | host | partial-pass: 167 sanitized UI tests, 5692 renderer frames; names/hiding/order persist | native idle-console check pending
- 2026-10-11 | G4-G6 | integrated features | host | partial-pass: 188 UI, 129 core, 34 media, 50 tooling tests passed | console .30 occupied; native build and remaining browsing in progress
- 2026-10-11 | G4-G6 | dc5d9e8 | host/PS5 build | partial-pass: 208 sanitized UI tests; Home, VOD details, account info, USB import linked | results/integration/console-76 | .30 occupied; no-run
- 2026-10-11 | G4-G5 / Sources | b929ae4 | host | partial-pass: 218 sanitized UI tests; cold Home episode continuation, source HTTP defaults and custom XMLTV integrated | .30 occupied by PPSA99008; no deployment
- 2026-10-11 | Sources / phone | 636a68e | host/PS5 build | partial-pass: 225 sanitized UI tests, full lint/static analysis (136 attributed files); arbitrary source headers and guide origin isolation | results/integration/console-77 | no-run
- 2026-10-11 | Home / phone | 7b24316 | host/PS5 build | partial-pass: 26 focused sanitized tests; phone browsing during playback, current Home URLs and large-list phone performance | results/integration/console-78 | .30 occupied by PPSA99008; no deployment
- 2026-10-11 | Home / quality | a48a8a5 | host/PS5 build | partial-pass: 233 sanitized UI tests; indexed Home parent policy and manual HLS quality switching | results/integration/console-79 | .30 occupied by PPSA99008; native acceptance pending

## Combined quality, household and language validation

Candidate `7e4475d` integrates source checking, crash-report recovery, viewing
limits, encrypted console sharing, manual and reduce-only automatic HLS quality,
Arabic/Hebrew shaping and layout, and guide/recording translations.

- All 276 combined interface tests pass under ASan/UBSan (117.4 seconds).
  The 120,000-channel app case takes 11.6 seconds after removing repeated
  category-tab geometry calculation. The shaping implementation separately
  passes 173 checks; Arabic/Hebrew host renders and an independent bounds/bidi
  review found no outstanding blocker.
- The integrated guide/localization checks pass all 22 focused tests, and the
  Auto-quality/quality-reopen checks pass all 10. Native-player static analysis
  and formatting/assets checks pass. The additive slow-network fixture verifies
  that high-quality segments exceed its bandwidth while low-quality segments fit.
- Application-only PS5 build case 86 passes: 41,815,324 bytes, SHA-256
  `2f161d937e5513523dc5a62c5a26c9b7baa5bb11ad8cc19cfe553952051ed4dd`.
  Case 85 exposed an assembly omission for C++ headers; the shared assembly step
  now copies both `.h` and `.hpp` files, and the clean rebuild includes Auto.

Evidence is in `results/integration/console-86` and the ignored integration test
logs. Console `.30` still reports `PPSA99008` running, so this candidate has not
been installed or run there. Runtime acceptance remains pending, including actual
quality reduction and sharing between consoles. Surround output still lacks a
verified PS5 output-route contract, and spoken focus has no verified SDK API;
neither is claimed implemented. Roadmap checkboxes remain open until acceptance.

## Source User-Agent presets

Candidate `6672d7f` adds the default, Smarters, VLC and custom chooser for #40,
with all eight translations and preserved source credentials/headers. Thirteen
focused sanitizer tests pass, plus two translated/RTL menu checks. The corrected
Arabic chooser was visually checked with large text and high contrast. Full
formatting/assets/metadata lint passes after adding the missing attribution to
the Auto helper (137 attributed files).

Application-only case 88 passes: 41,815,852 bytes, SHA-256
`73b0a1110a2d79fc87af6a9a13cc98a694a4c16843b988a001e1d3ddbdf6e522`.
Draft PRs #41–#46 separate the completed implementation groups; their bodies
identify the prerequisite order and pending native acceptance. No new console
installation or runtime acceptance is claimed. One obsolete worktree test cache
was removed, freeing 199,290,676 bytes without deleting source or current evidence.

## Host sanitizer build correction

CI exposed mixed RTTI flags across font ownership: app translation units created
shared-pointer control blocks without RTTI, while kit code used RTTI-enabled
UBSan checks when releasing them. A two-file Clang 18 reproduction using only
`make_shared<int>` fails with the same invalid-vptr report; enabling RTTI
consistently makes it pass without suppressing any sanitizer check.

Candidate `0854ff7` keeps host tests/previews consistent and adds a real-font
copy/move/reload/failed-load regression. All 281 integrated Clang 18 ASan/UBSan
tests pass (114.7 seconds), plus 173 standalone shaping checks. Native compiler
flags and application code are unchanged; case 88 remains the native candidate.
The correction is included in draft PRs #45 and #46. Console acceptance is still
pending while `.30` runs another title.

- 2026-10-11: idle .30 accepted merged main 958a68d (case 89: 56 deployed files verified; Home/Live/On Demand/Sources/Settings and 11,172-channel catalog) and native status-label fix ac2c4e9 (case 93: all seven tabs correctly identified; 20-second H.264 1920x1080 playback, 493 decoded/473 presented frames, 918 audio frames/3,672 output grains, zero audio/video queue underruns or presentation gaps over 500 ms, clean teardown; services 2121/3232/9021 reachable and no active title). Candidate ELF SHA-256: 505236d6f183e384e8c7a8a4fdd553821dff8797500c9221285ca5adaf3a84ca; evidence: results/integration/console-89 and console-93; disposable no-elevation title: PPSA88306. These bounded checks do not validate 4K, VOD, Auto-quality throttling, physical USB, surround or speech; controlled LAN fixtures remain blocked by inbound Windows firewall access.

## G1/G2: Public catalog scrolling

G1: Keep a full provider category row and the visible channel names in the bounded
text shaping cache. Verify cached measurement without relying on wall-clock timing.
G2: Compare Down and held R2/L2 on the public catalog in the same disposable title
on idle .30; require clean shutdown and service health. Preserve the regular app's data.

- 2026-10-11 | G1 baseline | e707301 | .30 PPSA88308 | reproduced: public catalog Down peaks 310 ms, R2 interval 50.62 ms mean | results/integration/public-scroll-before-30 | validate cache fix

The cache now retains 512 entries and evicts least recently used text; its existing
64 KiB text and 32,768 glyph limits remain. A wrapped HarfBuzz regression fails with
FIFO eviction and passes through 1,200 new channel names while reusing 200 categories.
All 6,203 shaping checks and 13 focused UI ASan/UBSan tests pass, including the
120,000-channel browsing test. Host public-catalog drawing fell from 17-20 ms to 2-3 ms.
Native .30 at 3840x2160, with logos and live preview enabled, completed Down and held
R2/L2 on 11,172 public channels. Scrolling frame windows average 17.27-17.77 ms instead
of 50.62-52.11 ms; worst scrolling window frames are below 58 ms instead of 310 ms.
Small frame-time spikes remain; this is not a claim of perfect 60 fps or a network fix.

- 2026-10-11 | G2 | e2637a1 | .30 PPSA88308 | pass: Down/R2/L2, clean close, services healthy | results/integration/public-scroll-final-30 | regular .060 build

## G1/G2: On-demand availability counts

G1: Show the selected source's movie and show counts, disable confirmed-empty
libraries, and distinguish unknown/loading results from zero. Reuse the search
loaders and permit empty caches only for VOD. Host checks cover mixed playlists,
source switching, hidden categories, refresh recovery and empty cache persistence.
G2: Verify both zero-count buttons remain on the overview after Cross in the
isolated PPSA88308 title on idle .30; require clean shutdown and healthy services.

- 2026-10-11 | G1/G2 | 3278945 | .30 PPSA88308 | pass: 35 UI sanitizer + 16 store tests; gray zero counts and blocked Cross verified; clean close | results/integration/vod-counts-30