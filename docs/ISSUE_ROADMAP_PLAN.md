# Provider reports and remaining interface work

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
