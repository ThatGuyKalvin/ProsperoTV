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
