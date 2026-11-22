# Roadmap validation, 22 November 2026

Candidate: `feat/roadmap-completion`, based on `74ec462`. The console and host runs included the audio changes tracked separately in PR #105; this branch contains the roadmap and VOD changes.
Console: PS5 firmware 4.50, as reported by the owner. The disposable test
title is `PPSA88919`; the installed `PPSA99003` monitor-audio build is preserved.

## Changes and host evidence

- Cancelling an On Demand download no longer reports a provider failure.
  A regression starts a delayed provider request, cancels it, and checks that
  the worker stops without publishing an error.
- Roadmap checkboxes distinguish implemented source features from outstanding
  physical-device and platform acceptance.
- All 338 interface tests across 50 suites passed with ASan/UBSan after fixing
  a Hebrew RTL fragment that dropped the leading space in `Showing the`.
- Repository lint passed; clang-tidy was skipped by the ARM64 container runner.
- Shared-source validation passed: 140 unit tests, 42 media tests, 62 tooling
  tests, audio behavioral checks, and five independent FFmpeg metering tests.
- Final production (`PPSA99003`) and disposable (`PPSA88919`) packages were
  rebuilt from the formatted source; both signed executable integrity checks
  passed. Production SHA-256: `52657b7963df5a0f60c230bc5939715299114b300f50f6cbaad4a6eccd825f94`.

## Console smoke test

All 61 installed files matched their uploaded bytes on FTP readback, including
the signed executable. The native container inspector reported valid integrity.
The existing ELF-loader service launched the disposable title successfully.
The `dev/no-elevation.txt` marker kept it in its own sandbox.

The script completed transitions through Home, Live TV, On Demand, Favorites,
Sources, Settings and About and saved eight screenshots. Steady-screen samples
averaged 16.52–16.56 ms per frame; the initial Home sample averaged 16.76 ms
and included a 50 ms maximum. On Demand and Settings screenshots were inspected
and were legible without visible overlap at the 1080p render size. The app
closed itself after the capture window; FTP confirmed its sandbox disappeared.

The initial smoke run used an empty fixture library and covered startup and
navigation only. A follow-up run loaded a disposable one-movie M3U from the
local fixture server, navigated into On Demand, and played its synthetic
H.264/AAC title for 18 seconds. The PS5 playback receipt reports success,
`stream_result=0`, no playback or stream error, audio enabled, and AAC at
48 kHz, mono. The script returned to the menu and wrote four current-run
screenshots. This establishes one controlled PS5 VOD playback pass; it does
not establish provider compatibility, multi-channel audio routing, or
subjective loudness on the Deco Gear monitor. The sandbox reported that the
console did not allow the phone remote. Physical controller usability and
monitor listening remain unassessed.
The current run report, including the playback receipt, is in
`/Users/unisechevez/prosperotv-diagnostic-20261122/audio-test-report.txt`.

Audio preset work is tracked separately in PR #105.

## Remaining acceptance and platform work

Cross-app picture in picture needs a supported cross-title display/input
contract. External-player handoff needs an installed receiving player and its
launch contract. Spoken focus needs a usable speech implementation. Source
inspection alone cannot establish support for any of these.

Physical surround requires a receiver and speakers; this owner's stereo
monitor cannot validate channel routing. Sharing requires a second console.
USB recording/export, console-user switching, PIN entry, subjective sync and
the revised audio presets also require explicit native acceptance. These are
not passes merely because their host tests pass.
