# Source, browsing and guide roadmap

The development branch implements the sixteen requested items. This document
describes the implemented contracts; it does not claim that a release has been
published or that every provider has been tested. Later roadmap features and
the current acceptance status are recorded in [the roadmap status](ROADMAP_REMAINING.md).

## Sources and categories

Sources keeps multiple playlists, Xtream accounts and Stalker/Ministra MAC
portals. Cross selects a saved source, Triangle edits it, and the add rows create
another source. Each source has its own cache. Square cycles daily, weekly and
manual refresh; a missing cache is downloaded once even in manual mode.
Options explicitly refreshes the selected list. The guide and on-demand lists
follow the same source schedule.

Touchpad in Live TV opens provider categories. Cross descends into a parent
or opens its channels; Browse all includes descendants. Circle returns to the
parent. Square hides or shows a category, including its descendants. Hidden
entries remain in this management sheet so they can be restored. Visibility
applies to browsing, favorites, search and the guide.

Provider parent IDs and category names separated by `/`, `|` or `::` form
hierarchies such as US → Sports → Football. A name without hierarchy metadata
remains one category; the app does not guess a country's parent from its name.

MAC portals take the provider's portal address and MAC code. The app performs
the provider handshake and profile check, reads live genres and channels,
and obtains a fresh playback URL with `create_link`. Commands returned by the
portal are parsed as data. MAC cookies and bearer tokens stay on their origin
when a request redirects. Provider-specific device registration requirements
beyond the standard MAC profile are not emulated.

## Browsing and favorites

PNG and JPEG logos appear in channel tiles and the large television; failed
artwork falls back to initials. Downloads and decoded texture memory are bounded.

Favorites supports 65,536 channel IDs. Touchpad in Favorites opens folders.
Triangle creates a named folder, Square adds or removes the focused channel,
and Cross opens the folder's list. Membership survives source switches and
restarts. Existing favorites migrate without changing their IDs.

Settings contains Hide channels that failed, Start on the last channel and
Live previews. A preview starts after 1.2 seconds of stable focus, without
audio. It stops when focus changes, a sheet opens, the source changes, or
normal playback begins. Its independent HTTP connection cannot cancel a guide
or source download. Preview failures do not change channel health or history.
The native decoder supplies NV12/P010 pictures; a bounded 640×360 CPU conversion
updates one OpenGL texture on the renderer thread, at up to 12.5 frames/second.
Preview supports the existing H.264/HEVC MPEG-TS and unencrypted TS-based HLS
paths, including MAC portal URL resolution.

Startup playback uses the last selected live channel and its saved source,
unless it is hidden or its recorded playback failed. Input during startup
cancels automatic playback. Movie episodes and
archived programmes do not replace the remembered live channel.

## Programme guide

M3U `url-tvg` / `x-tvg-url` and Xtream's XMLTV endpoint supply the guide. Both
plain XMLTV and gzip are supported. Matching uses channel IDs, then unambiguous
names. Now/next appears beside the channel; ordinary search also matches the
programme currently on air.

R3 opens the guide. D-pad moves through channels and programmes, L1/R1 changes
the day, L2/R2 changes the channel page, Square returns to now, Triangle searches,
and Options refreshes the guide. Cross watches the live channel or a selected
past programme when the provider advertises a compatible archive. Supported
archive forms are Xtream timeshift and M3U `default` / `append` catch-up URLs.
Future programmes cannot be played. Time offsets and archive retention are
checked before constructing playback URLs.

The guide retains a bounded fourteen-day window in either direction, with
limits of 500,000 programmes and 128 MiB of text. Failed refreshes preserve
the last good cache. External XML entities are not expanded.

## On demand

Select an Xtream account or a classified M3U playlist, then open On demand
beside Live TV. Movies and TV shows have
provider categories, including subcategories and the source's visibility
choices. Shows open seasons and numerically ordered episodes. Triangle searches
the current list; Options refreshes it. Lists and episode catalogues have
separate saved caches, loaded before a scheduled refresh.

MP4 and Matroska H.264/HEVC packets are remuxed into the existing MPEG-TS player.
HTTP byte ranges let MP4 metadata be read from either end of a file. Video is
still decoded by the native backend; supported audio uses the existing audio
pipeline. The later audio/subtitle work adds track selection and supported HLS
renditions; its limits and validation are recorded in the roadmap status. DRM,
encrypted HLS and unsupported video codecs remain outside this implementation.
The On demand provider API supports Xtream. Extended M3U playlists also supply
movies and episodes when their entries identify the media type; these use the
playlist cache and each entry's playback headers. MAC portals provide live channels.

## Original source/browsing/guide validation

These are the original checkpoint results; the roadmap status records subsequent
fixes, native cases and integrated acceptance.

- All 112 host UI tests pass under ASan and UBSan, including nested category visibility,
  multiple-source persistence, guide matching/timezones/catch-up, portal header
  scope, VOD navigation/cache failures and preview cancellation/frame ownership.
- The 72 core tests and native/tooling regressions pass. Three container tests
  generate synthetic H.264/AAC MP4 and Matroska plus HEVC MP4,
  then feed their real compressed packets through the production demuxer and
  stream parser. Generated media stays under ignored `build/`.
- The PS5 cross-build includes the complete interface and native preview path.
- Host renders cover categories, guide, movies, seasons, episodes, Settings and
  a synthetic preview inside the television. These renders are not hardware
  evidence.
- Console validation used idle console `192.168.4.30`, disposable title
  `PPSA88261` and its own sandbox, with elevation disabled throughout. Both
  runs ended with no active user title and all three development services
  answering. Evidence is retained locally in `results/roadmap/console-01/`
  and `results/roadmap/console-02/`.
- The first run (`50c7bad`) verified HLS/TS previews inside the television,
  logos, nested categories, now/next, guide/archive navigation, programme search,
  source switching, movie/season/episode browsing and native MP4/Matroska playback
  with AAC. It exposed an unresponsive stop while buffered video drained.
- The corrected candidate (`bf5b95d`) keeps polling controls while read-ahead
  and native video queues drain. A completely buffered ten-second MP4 and a
  longer Matroska movie stopped at 5.042 seconds; an episode stopped at 4.042
  seconds and portal live playback at 5.025 seconds. All reported presented
  video, decoded audio, the requested stop and successful cleanup. Preview
  diagnostics reported delivered video, zero decoded audio and successful
  cleanup; menu samples averaged about 16.7 ms per frame.
- The second script's initial source-switch input arrived during the startup
  animation, so its playlist/guide screenshots are not acceptance evidence.
  Those paths are covered by the first run; the second validates the buffered
  stop correction and repeats VOD and portal playback.
- No real provider credentials were supplied. Protocol compatibility is tested
  with synthetic responses, not claimed for every provider.


### USB playlist files (host implementation; native acceptance pending)

Sources offers **Find playlists on USB** and import rows for top-level `.m3u` / `.m3u8`
files under the normal `/mnt/usb0` through `/mnt/usb7` mounts. Imports become independent
saved Custom sources; existing playlists and accounts are retained. New USB sources default
to manual refresh. Their parsed catalogue is stored privately in the app, so saved channels
remain available after disconnecting the drive or restarting. Refresh rereads the original
USB path; daily/weekly schedules apply if selected. A missing drive or failed read keeps the
last good catalogue. Reconnect the same drive at the same mount path to update it; importing
the same path selects and refreshes its existing source. Playlist media addresses must still
be HTTP(S); this feature does not import local video files or follow relative video paths.

Discovery examines at most 4096 entries per drive and offers at most 64 playlist files.
Names are at most 128 bytes. Only root-level regular files are accepted; hidden names,
traversal, symlinks, directories and special files are excluded. Reads stream in 64 KiB chunks,
with the existing 512 MiB playlist bound, catalogue limits and cancellation. File changes,
short reads and mount changes fail without replacing the cache. No elevated USB namespace is
used. Physical drive recognition and native interaction remain unverified.


### Custom XMLTV and channel matching (host implementation; native acceptance pending)

On Sources, L2 edits that source's HTTP(S) XMLTV address. Empty restores the provider's
playlist/account guide. In the full guide, Options > Match XMLTV channel edits the selected
visible live channel's XMLTV `channel` ID; use the ID from the guide, not its display name.
Empty restores automatic `tvg-id` and unambiguous-name matching. Explicit matches take
priority, and several live channels can share one guide ID. An unknown guide ID produces
no programmes for that channel rather than guessing a replacement. There is no station
picker: matching uses the focused ID input.

Overrides and up to 4096 explicit matches per source persist in the private Library database
(and its existing backup). Guide IDs are limited to 256 bytes. Source account/address changes
clear their old guide configuration; renaming a source retains it. The saved guide validates
both source identity and the URL/mapping fingerprint, using one guide database per source
rather than accumulating a new cache after each edit. Editing configuration invalidates old
programme assignments immediately; successful downloads save the replacement. Offline restart
keeps the last good guide only when its configuration still matches. Existing parental and
hidden-channel filtering remains authoritative. Native keyboard/display acceptance is pending.
### Home episode continuation (host implementation; native acceptance pending)

Resuming an episode from Home prepares an independent series list, without moving
On Demand to that show. A saved list is usable immediately, including offline. If
it is missing, one series request runs through the shared menu download gate while
controller input remains active. Circle, changing tabs, choosing another source or
refreshing cancels the pending playback. After at most ten seconds plus request
cancellation, the saved episode starts even if its series could not be downloaded;
a warning explains that automatic continuation is unavailable. A later Home resume
retries the missing list. There is no background retry during foreground playback.

At normal end-of-file, the next episode in provider season/episode order is queued
only for the same source record, source fingerprint and current series episode.
Playlist episodes retain their bounded category/title series key across restart.
Hidden channels/categories, inherited show restrictions and current parental rules
are checked again. A locked next episode is not skipped. Its own stream URLs and
HTTP headers are retained. Manual stops, playback failures, sleeping, and the final
episode do not advance. The independent list remains available when menu workers
are closed for foreground playback.

Native acceptance still needs a restarted app: resume an unfinished episode from
Home, seek near its end, and confirm the next episode starts while On Demand keeps
its previous selection. Also verify saved-cache playback offline, Circle/source
change during a missing-list request, and fallback to the saved stream when that
request fails. Host tests cover these model transitions and the ten-second bound;
they do not establish console playback or controller behavior.
### Phone channel and guide pages (host implemented; native acceptance pending)

A paired phone can open Channels and guide, search channel names/categories and programmes
on now, page through up to 40 live channels, see now/next, read up to 48 upcoming programmes
per guide page, and tap a channel to play it on TV. Responses contain channel IDs/display text
and programme times, never stream URLs or provider credentials. DOM text nodes display provider
content. Existing TV keyboard-search and remote controls remain available.

The authenticated `/api/browse` POST endpoint applies the existing pairing cookie and custom
header requirements. It limits request size and page output, validates source identity on guide,
paging and tuning requests, and rechecks parental/hidden/failed-channel policy before returning
channel data or playing. During playback a read-only view borrows the stopped Model's catalogue
and guide and copies visibility marks, order and renamed labels once. Menu requests borrow
those existing arrays synchronously, so a small page does not copy or recheck the entire source.
The synchronous polling handler records a selection without mutating the Model. Idle player polling honors the normal
stop-for-switch result; the handler detaches immediately after playback, before its view can
expire. The Model then rechecks source and content policy before queuing the channel. Tapping
the current channel does not restart it. A phone switch saves VOD progress without advancing
to the next episode. Source editing still requires the menu. Host tests cover stopped-Model
browsing/search/guide, deferred tuning, blocked selection and final policy/source revalidation.
Native acceptance remains pending; roadmap stays unchecked.

A host ASan/UBSan benchmark with 250,000 generated live channels and ten first-page requests
measured 199,472 microseconds per request before borrowing menu visibility data and 177 after.
The regression checks that menu views hold no owned catalogue-sized arrays/maps; timing is
reported, not used as a flaky pass/fail limit. PIN-prompt snapshots reject browsing, and moved
playback snapshots retain renamed labels and visibility independently of later menu changes.

### Manual HLS quality (host implementation, console acceptance pending)

Playback Options now has a Quality tab listing compatible master-playlist variants and the
currently attempted/playing selection. Best available uses the existing resolution/codec/bitrate
ranking; it is not bandwidth-adaptive streaming. Direct files, media playlists and single-quality
masters explain that no alternative is supplied. Choices last for the current foreground stream.

Changing quality cleanly closes the player, preserves the full authenticated request, VOD
position, pause state, speed and audio delay, then re-fetches the original master. Audio and
subtitle selections are matched by language/title/accessibility role, with stable-ID fallback
only when metadata is absent. Missing audio leaves the replacement default playing with a notice;
missing subtitles stay off with a notice. An explicit Audio Off choice remains off. Live playback
restarts at the live edge and discards its replay buffer. Phone channel selection takes priority.
Quality changes save progress without marking completion or advancing
episodes.

The fresh master must contain a compatible descriptor, or a compatible fallback no larger in
resolution or advertised bandwidth. Reordering cannot silently pick a different numeric index.
Recognized, fully validated unencrypted fragmented-MP4 media playlists now use the container
path; encryption encountered after EXT-X-MAP still fails explicitly. Direct MP4 stays a single
quality and the existing bounded manifest/HTTP origin policy remains in force.

Native acceptance: exercise a multi-variant TS live stream and fragmented-MP4 VOD; change down
and up while paused and while playing, verify position/tracks/speed/delay and unchanged headers,
then reorder/remove the selected variant and fail its segment requests to check bounded fallback.
Check direct MP4 and single-quality HLS, encrypted rejection, Circle cancellation, simultaneous
phone selection, and long translated Quality panels. This change has no hardware verification.


### Surround sound feasibility (not implemented)

The current FFmpeg build can decode multichannel AAC/AC-3/E-AC-3, but the decoder,
limiter, native sink, tempo wrapper and loudness normalizer deliberately produce or assume
stereo. A 5.1 implementation needs explicit layout propagation, larger bounded PCM buffers,
channel-aware processing and a verified mapping into an eight-slot native output port.

The installed SDK v0.42 exports the AudioOut Ex configuration and HDMI monitor-query
functions, without typed headers. Public [PS5 Audio Passthrough Research](https://github.com/sainsaji/PS5-Audio-Passthrough-Research/tree/c3be9c2afcb8f7838784ead0fe1c12a2ceea965a)
reports sandboxed 5.1 PCM on PS5 Pro firmware 12.70. Its [output implementation](https://github.com/sainsaji/PS5-Audio-Passthrough-Research/blob/c3be9c2afcb8f7838784ead0fe1c12a2ceea965a/sample-app/src/platform/ps5/bitstream_out.cpp)
uses standard AudioOut format 2 and HDMI mode 6. However, its capability-buffer offsets
come from one firmware dump, and HDMI capabilities do not establish the active headphone,
USB or HDMI route. These are not sufficient evidence for an automatic surround switch.
Keep stereo until the target firmware's capability structure, active route, speaker order,
stereo-only fallback, hotplug and repeated switching are verified on hardware.

Encoded passthrough is a separate larger change: bounded IEC61937 packing, receiver delay,
exclusive output ownership and defined volume/normalization/speed behavior. The upstream
[findings](https://github.com/sainsaji/PS5-Audio-Passthrough-Research/blob/c3be9c2afcb8f7838784ead0fe1c12a2ceea965a/docs/FINDINGS.md)
also report unresolved receiver lockups after repeated format changes. No surround setting,
new dependency or console probe was added for this investigation.
### Explicit source checking (host implementation; native acceptance pending)

Sources offers a Check/Cancel active source row after saved sources, with passed/failed/skipped totals.
Only visible, permitted live channels are checked. One muted preview connection runs at a time,
with a 15-second channel budget and one second between channels. Actual decoded pictures pass;
HTTP 200 and decoder-open alone do not. A channel fails only when all entry URLs return 404/410.
Missing HLS segments, unsupported codecs/containers, timeouts and interrupted checks never mark
a channel failed. HTTP 401/403/429/5xx and portal resolution failures stop the batch. Full playback,
previews/PiP, recordings, source changes, refreshes and parental prompts stop checks before taking
those resources. No automatic mass checking or restart occurs.

Results use separate per-source .checks.sqlite3 files next to catalogues, bounded to 250,000 rows
and 64 MiB. Up to 32 results commit together, or after five seconds; cancellation flushes completed
results. Inconclusive checks preserve prior confirmed health. Source/account/request-header identity,
channel URL fingerprints and timestamps prevent cross-source or stale overlays. Newer manual
playback history wins. These files never replace or evict normal playback history. Tests exercise
classification, first-frame evidence, cancellation, policy changes, batching, caps and precedence.
Preview codec coverage remains narrower than full playback; skipped counts make this explicit.
Physical console decoder/provider acceptance is pending, so the roadmap remains unchecked.
### Kids viewing limits (host implementation; native verification pending)

The current console profile can set a daily viewing-session allowance and a bedtime/wake
window in Settings after unlocking the parent PIN. Limits apply only in kids mode and
remain active during a session PIN unlock. Turning kids mode off requires that unlock.
The displayed wake time starts each allowance day. The console's local clock is displayed
beside the controls; an overnight bedtime window does not reopen at midnight.

Session time includes pauses, buffering, tuning and visible previews. A mosaic or PiP
session counts once, not once per channel. Menu browsing, source health checks and background
recording do not consume viewing time. All normal playback requests, phone-selected channels,
Home resumes, automatic episode continuation and quality switches share the same check.

Allowance accounting reserves at most 30 seconds in the profile's atomic parental file
before viewing. An orderly stop returns whole unused seconds; a crash/power loss can lose
up to 30 seconds. Reopening cannot recover a committed reservation. Backup stops preview
sessions and settles their reservation before copying settings. Invalid settings or failed
usage writes block viewing rather than resetting the allowance.

The console clock is not trusted time. Missing/invalid time or a backward jump exceeding
two minutes blocks configured limits; repeating a local day never resets its allowance.
A forward clock change across app restarts cannot be distinguished reliably from elapsed
time; backward changes that stay after the last persisted clock observation can also go
undetected after a restart. This is a household convenience, not tamper-proof enforcement against a user with
console clock or profile-file access. Native local timezone/DST and real playback stop
behavior still require console testing. No roadmap completion box has been changed.

## Fatal-signal crash reports

The app preopens a profile-local 32-byte record before starting application workers.
Default fatal handlers for ABRT, BUS, FPE, ILL and SEGV record only signal/code, coarse
startup/menu/playback phase and time, then preserve the operating system's fatal action.
Existing platform handlers are left alone. No account, stream address, stack or memory
contents enter the record. A normal close leaves no crash report.

The next launch atomically saves a redacted failure report, shows where to export it in
Settings and skips automatic last-channel playback for that launch. Failed recovery
preserves the pending record. The report omits the app version because the crashing
binary might have been replaced before recovery. The existing USB export sanitizer
allows only the new numeric crash fields, alongside playback diagnostics.

`opengl-ui/tools/check-crash-report.sh` passes 114 unsanitized host checks, including
all five signals, preserved termination, custom/ignored handlers and malformed files.
Capture is best effort: stack exhaustion, forced termination, power loss and hangs may
produce no record; a blocked filesystem may delay a fatal handler. Native signal
delivery and export still require console validation. No console crash was induced.

Integrated validation at `ba9182a`: the PS5 application links successfully (40,287,388-byte
ELF, SHA-256 `eef0c65391ece71c5dee35b404ed1b8579e64ff1159ea4856edc63a82380fb31`),
with evidence in `results/integration/console-81`. The 251-test sanitizer run passed
250 tests; editing a translation during compilation left its implementation object stale.
Rebuilding the stable tree passed all six targeted translation, report-redaction and
source-check tests. The additional checker/viewing-allowance integration regression
passes in the root tree and proves checks preserve the parental file byte for byte,
including after quota exhaustion. The latest console preflight found `PPSA99008_000`
running, so this candidate has not been installed or exercised on the console.

The subsequent sharing integration passes all 262 UI sanitizer tests on a stable
source tree. The complete static-analysis run identified a quality-candidate count
initialization path; resetting the builder's count explicitly resolves it and the
affected translation unit passes its rerun. Formatting, metadata and asset lint
passes all 136 attributed files. Encrypted sharing and its SDK build evidence are
described in [CONSOLE_SHARING.md](CONSOLE_SHARING.md).
