# Plan: player controls

## Goal
Films and episodes (MKV/MP4 through `src/iptv_media.cpp`) get:
- pause/resume;
- seeking with a timeline;
- resume where you stopped;
- a choice of audio track.

Live TV gets an info banner: channel, now/next from the guide, and the time. Everything is drawn as a colour bar over the video and fades after a few seconds. Subtitles are left for later.

## Controls
| Button | Films and episodes | Live TV |
|---|---|---|
| Cross | Pause / resume | Show the info banner |
| Left / Right | Back / forward 10 s; holding repeats, in growing steps | Show the info banner |
| Up / Down | Forward / back 60 s | Show the info banner |
| Triangle | Next audio track | Show the info banner |
| Square | Start over (offered when playback resumed) | Show the info banner |
| Circle / Options | Stop and return to the menu | Stop and return to the menu |
| Touchpad + R1 | Stats (unchanged) | Stats (unchanged) |

- Any button shows the bar. It hides 4 s after the last press; while paused it stays.
- Seeks are combined: holding Left moves a target time shown on the timeline, and the seek runs 400 ms after the last press.

## Drawing (`src/iptv_osd.c`, `include/iptv_osd.h`; pure C, host-tested)
- The presenter already draws its stats overlay on the CPU, straight into the decoded 4:2:0 frame:
  - 8-bit NV12, or Main10 with 10 bits in the low end of each 16-bit word;
  - Y plane first, then interleaved UV at `pitch * height`.

  The OSD does the same with colour, writing both planes.
- **Only writes, no reads.** Panels are opaque, and text is anti-aliased against the panel's known colour. Reading back decoder memory (which may be uncached) is never needed, and the cost is proportional to the bar's area.
- **Font:** Montserrat from `ui/fonts/lvgl-bitmap`. `tools/generate-osd-font.py` turns two sizes into `src/iptv_osd_font.h` (alpha bitmaps for ASCII 32–126), so nothing is loaded at run time and no new `/app0` path is added.
  - Text size follows the frame height (frames are drawn at their decoded size and scaled by the GPU).
- **Hand-off between threads:** the player thread publishes an `iptv_osd_state_t` (kind, title, line, position, duration, paused, track label, hint, clock) through a seqlock. The video worker takes a snapshot of it for each frame.
- **Layout:**
  - **Films and episodes:** a bottom bar with a title row and the track label at the right. Below that, a timeline:
    - track in grey, elapsed part in orange `#f3a447`, and a white knob;
    - "12:04" on the left and "-1:02:10" on the right;
    - a "PAUSED" badge when paused;
    - a row of button hints.
  - **Live TV:** a bottom banner with the channel name, now and next lines, and the clock.

## Backend (`src/iptv_native_backend.c`)
- **`iptv_native_backend_set_paused(backend, paused)`:**
  - The video worker idles before the underrun check, so the start gate is not re-armed. On resume it rebases pacing itself.
  - The audio worker idles; the output simply underruns into silence.
- **`iptv_native_backend_seek_reset(backend)`:**
  - The generation is increased, the start gate is re-armed with a short (500 ms) threshold for this restart, and both workers drop stale items. The decoder is reset on the first new frame, as for discontinuities.
- **`iptv_native_backend_presented_pts(backend)`:** an atomic copy of the PTS of the frame on screen.
- **Redraw while paused:** when the OSD state changes while paused, the video worker draws it on the last presented output and presents that again. The image under the bar is already overwritten, so while paused the bar stays.
- **Changing audio format:** when the generation changes, the audio output is reopened and the decoder recreated if the audio type, rate or channel count changed. This is set with `iptv_native_backend_set_audio_type(backend, type)` before `seek_reset`.

## Media player (`src/iptv_media.cpp`)
- **`Sink.control(context, MediaControl*)`:** returns the next request: toggle pause, seek to an absolute time, next audio track, or start over.
- **`Sink.status(context, const MediaStatus*)`:** reports the duration, position, paused state, the list of audio tracks and the one in use, for the OSD.
- **Pause:** while paused, the loop stops reading packets and only polls controls (at 20 ms intervals). The 15 s stall check is suspended.
- **Seek:**
  1. `avformat_seek_file(target)` to the keyframe at or before the target;
  2. `av_bsf_flush` on both filters;
  3. `seek_reset` in the backend;
  4. audio packets earlier than the first video frame after the seek are dropped.

  The HTTP reader restarts its download at the new offset (one connection).
- **Audio track:** the next supported track is selected, its filter rebuilt, the backend's audio type set, then a seek to the current position.
- **Resume:** `Source.start_position_us` makes playback seek once before it starts. Square seeks to 0.

## Player (`src/iptv_player.cpp`, `include/iptv_player.h`)
- **`StopRequested`** keeps handling Circle/Options and the stats chord. Every other button press goes into a small control queue that the media `Sink` and the live banner read.
- **`StreamRunner`** composes the OSD state and publishes it.
- **API:**
  - `iptv_player_run_options(const iptv_player_options_t *)` takes the URL, headers, `reconnect_live`, VOD flag, start position, title and live now/next lines.
  - `iptv_player_last_position_us()` / `iptv_player_last_duration_us()` report where playback ended.
  - The old entry points (used by opengl-ui) stay as wrappers.
- **Live TV (TS/HLS):** the banner shows for 5 s at start and on any button press; its clock is formatted from `time()` / `localtime`.

## App
- **`IptvPlayRequest`** gains `vod`, `start_position_us`, and `info_now` / `info_next`.
- **`IptvApp::QueuePlay`:**
  - For films and episodes it loads the resume position (`LoadResumePosition`).
  - For live channels it fills now/next from the guide (`GuideReader::NowNext`).
- **`src/main.cpp`:** passes these to the player. After a film or episode returns 0 or 1, it calls `SaveResumePosition` with the reported position and duration. The store already drops positions in the first 10 s or the last 30 s / 5 %.

## Order
1. OSD drawing, the font generator and the embedded font, with host tests that draw into NV12 and P010 buffers.
2. Backend: pause, seek reset, presented PTS, OSD snapshot and redraw while paused, audio type change.
3. Media player and player: controls, status, pause, seek, track change, resume start; the live banner; the new API.
4. App: request fields, resume load and save, guide lines.
5. Console build, deploy and on-device testing.

## Constraints
- `opengl-ui/ps5/patch_tree.py` exact-text patches must stay intact:
  - the `AdapterOpen` tail, `Notify`, the receipt literal and the `iptv_player.h` include;
  - in the presenter: the `sceAgcInit` block, the loading memset, the slice loop, the `present_cancelled` pair, the `set_overlay_enabled` signature, the Montserrat path and the header include;
  - in the backend: the `started = monotonic_us();` pair and the `init` signature.
- Also no new `/download0` or `/app0` literals.
- New files are `src/iptv_*` so the opengl-ui assembly copies them.

## Follow-up: settings menu (aspect ratio, zoom, subtitles, audio delay)
- **Options opens a settings panel** over either layout (`iptv_osd_state_t.menu_*`). Up/Down choose a row, Left/Right or Cross change it, and Circle or Options closes it. Circle outside the panel stops playback; Options before the first picture still stops it.
- **Picture placement (`src/iptv_picture.c`):**
  - `iptv_picture_layout` turns the frame size, its pixel shape, the aspect mode and the zoom mode into a rectangle on the screen, plus the part of the frame that is visible.
  - The presenter programs that rectangle as the viewport (`PA_CL_VPORT_*`); the screen scissor cuts off what a zoom pushes past the edges.
  - A framebuffer that last held something else outside the new rectangle is cleared once, after the flip that shows the other buffer.
  - The pixel shape comes from the H.264 VUI on MPEG-TS, and from `av_guess_sample_aspect_ratio` for MKV/MP4.
- **Screen overlay:** the controls, subtitles and statistics line are drawn into an overlay of their own, not into the decoded frame. Drawing into the frame moved them with the zoom, and the decoder carried them into later pictures that refer to that frame.
  - The overlay is 1920x1080, in the video's sample format, and made of two surfaces: premultiplied colour and coverage (as luma).
  - After the picture, two more draws cover the screen. The first darkens the picture by the coverage (`CB_BLEND0_CONTROL` dst × (1 − src)); the second adds the colour (dst + src).
  - It is redrawn only when the OSD state, the subtitles showing, the statistics text (twice a second) or the picture's place changes, and only the rows used are cleared and flushed.
  - While the settings are open, subtitles centre in the space to their left.
  - When the picture does not fill the screen, the whole screen is first painted black by stretching a 64x64 black picture over it. The overlay is blended over the bars too, so without this they kept and built up what earlier frames left there.
- **Subtitles (`src/iptv_subtitle.c`):**
  - FFmpeg decodes the chosen track. Its decoders are subrip, srt, ass, ssa, movtext, webvtt, text, pgssub, dvdsub and dvbsub.
  - Text cues lose their ASS styling except italics and top placement, and are drawn in Inter SemiBold (`tools/generate-subtitle-font.py`: Latin, Greek and Cyrillic) with an outline.
  - Picture cues are scaled from their canvas to the frame.
  - The player thread renders the cues due in the next 6 s; the video thread blends the one due at each frame's time.
  - Subtitles sit above the controls when those are shown.
- **Audio delay:** a process-wide setting in the backend. Positive values output silence first; negative values skip that much sound. It is reapplied at each generation (start, seek or track change).
