# Decoded 5.1 audio

Settings > Audio output offers Stereo (the default) and 5.1 surround. Choose
5.1 for a surround-capable HDMI receiver or soundbar; it applies to the next
playback. The setting is saved per profile, included in settings backups,
translated, and protected by the existing parent lock.

AAC, AC-3 and E-AC-3 software decoding preserves six PCM channels in
FL, FR, FC, LFE, BL, BR order. Side-surround layouts are remapped and 7.1
sources are mixed to 5.1. Stereo sources retain silent rear channels. Playback
speed, sample-rate conversion, user volume and optional loudness normalization
retain the channel layout. The loudness meter excludes LFE; the linked peak
limiter still protects it. Menu previews and multiview keep stereo output.

The console opens a 48 kHz eight-channel S16 AudioOut port, using the existing
typed API. The six channels occupy their matching slots; the two unused side
slots are zero. If opening that port fails, the app downmixes to stereo. If
output later fails, it closes the surround port and retries the current grain
once through a stereo port. A failed stereo output is reported normally.
Centre/dialogue content is retained in the stereo fallback.

This is decoded PCM, not Dolby/DTS bitstream passthrough. An accepted
eight-channel port does not identify the connected speakers or guarantee that
the console's current HDMI, headphone or USB route exposes discrete surround.
The app does not change console settings or use firmware-specific offsets.
Diagnostics report decoded PCM channels, opened output-port channels and the
number of stereo fallbacks separately.

The output contract follows [ProsperoLight's 5.1 AudioOut path](https://github.com/blackbearreloaded/ProsperoLight/blob/143561ae04f38b8887cf2ae2ff59d37c0b21a17a/src/moonlight_stream.cpp)
and [EVO Player's PCM output](https://github.com/sainsaji/EVO-PLAYER-PS5/blob/9ee9a5420c73b5be448610e5f0a90a29a75592f3/projects/evoplayer/core/src/services/PlaybackController.cpp).
Their private bitstream/HDMI-route mechanisms are not used.

## Acceptance

- G1: host speaker-isolation tests for AAC/AC-3/E-AC-3, side/back mapping,
  7.1 downmix, buffer limits, tempo and normalization; simulated port failures
  must preserve dialogue and continue submitting PCM after fallback.
- G2: build and hash a committed candidate, then run bounded synthetic AAC,
  AC-3 and E-AC-3 playback on idle .30 as disposable title PPSA88307 with
  normal application permissions. Require six decoded channels, an accepted
  eight-channel port, audio/video output and clean teardown. Use the approved
  FTP 2121, klog 3232 and elfldr 9021 services under the shared console lock.
- G3: physical speaker identification and route changes require a compatible
  receiver and a listener. These are not inferred from port-open success.

G1 passed: all 282 UI sanitizer tests, 33 tooling tests, audio normalization
checks, multichannel tempo tests, native sink sanitizer tests, lint and focused
native static analysis.

G2 passed on 2026-10-11: case94, candidate `6342367`, PPSA88307 on idle .30;
AAC/AC-3/E-AC-3 each ran 12 seconds with PCM6/output8, no fallback or audio
queue underrun, and normalization enabled. A 10-second AAC stereo control
reported PCM2/output2. All four runs presented video and audio, reported no
audio errors, and cleaned up successfully. The title closed and all approved
services remained reachable. Screenshots confirmed both setting values.

Tested ELF SHA-256:
`ecc835bce2e94dd2d4a04c5f8e76c798152f05e052c9e050b7c44e0c9d01704b`.

G3 remains untested: the available setup has TV speakers/headphones, without
a surround receiver. Discrete physical speaker routing and live route changes
must be verified on compatible hardware. Stereo remains the default.
