# Optional channel volume normalization

`Settings → AUDIO → Normalize channel volume` offers Off, Standard, Louder and
Speaker boost. Off is the default. Existing saved `0` remains Off and saved `1`
continues to mean the original Standard behavior. The setting is saved per
console profile. The normal Volume control still applies afterward, including
mute. Interface sounds do not pass through this processor.

The processor runs on the common S16, 48 kHz sink after channel mapping and
resampling. It therefore covers native AAC and software audio paths without
changing codec selection or downmix coefficients.

## Control and limits

- K-weighted energy uses the BS.1770 48 kHz filter coefficients for two, six,
  or eight channels. LFE is excluded from loudness; surround energy is weighted
  by 1.41.
- Up to thirty accepted 100 ms energy windows drive a rolling estimate; at least
  five accepted windows are needed before leveling begins.
- Standard targets -23 LUFS and keeps the original limiter. Louder targets
  -18 LUFS; Speaker boost targets -14 LUFS. All presets keep gain in
  [-18, +18] dB and increase it by at most 3 dB/s.
- Windows below -50 LUFS are excluded from the program estimate. During quiet
  blocks, gain returns toward unity instead of rising. This cannot distinguish
  background noise above the threshold from intentional audio.
- Standard retains the existing linked block limiter. Louder and Speaker boost
  use a 5 ms look-ahead window and a linked per-sample gain envelope with a
  250 ms release. The ceiling is -1 dBFS **sample peak**, not dBTP.
- The louder presets delay sound by 5 ms internally; playback clock timestamps
  account for that delay. Normal end-of-stream drains the buffered tail.
- Channel/track changes, stream discontinuities, and toggling the feature reset
  history. Disabling the feature passes samples through unchanged.

This is a bounded playback leveler, not a certified integrated EBU R128 meter.
It does not guarantee identical subjective volume for every program, cannot
repair already-clipped source audio, and cannot recover absent audio tracks.
Boosting very quiet audio may still stop below the target because of the gain
cap. Very faint audio below the gate is deliberately not boosted.

## Diagnostics

Playback receipts add `audio_normalization_enabled`,
`audio_normalization_gain_millidb`, `audio_normalization_loudness_millilufs`, and
`audio_normalization_limited_blocks`. Loudness describes the input to the
normalizer (after downmix), not its output. `INT32_MIN` means the estimate is not
acquired. Values reflect the last submitted output block.

## Validation

Run `make test-audio-normalize` for sanitizer-backed behavioral checks of
bypass, silence, tiny signals, gain bounds, slew limits, transient limiting,
chunk-boundary continuity, linked channels, LFE exclusion, preset changes and
reset behavior. Run `make test-audio-normalize-ffmpeg` for independent FFmpeg
checks of K weighting, all three targets, maximum boost and bypass. The UI tests
cover all choices, persistence, legacy defaults and the Volume control.

Before release, build the OpenGL app with the existing Linux CI toolchain and
listen on PS5 with each preset. Compare a loud channel, an ordinary channel,
and a consistently quiet channel; then test silence, a sudden loud scene, mute,
track switching, channel switching, end-of-stream and restart persistence.
Keep speaker volume moderate during the comparison. Host tests do not validate
PS5 runtime linkage, CPU cost, device output, or subjective pumping/distortion.
