# Automatic HLS quality reduction

The playback Quality menu keeps **Best available** and manual renditions, and adds
**Auto (reduce on buffering)**. Auto is explicit and lasts only for this video or
channel, including its quality restarts. It never increases quality. A new channel
or video starts with the existing default. Choosing a manual rendition disables Auto.

Auto needs a master with at least two compatible renditions with declared nonzero
bitrates. Direct files and single-rendition playlists retain their existing behavior.
Encrypted/unsupported inputs do not gain support through this setting.

The player measures completed media reads and connection-opening waits, excluding
decoder/presentation time, playlist refresh waits, and recognized initialization
maps. Both the live TS path and recognized fMP4/VOD HLS resources feed one bounded
session policy. After five seconds with presented video, two consecutive windows
of at least two seconds must report throughput below 1.15 times the current declared
bitrate. The second window must also show a new native queue underrun or at least
one second without presented-frame progress. Windows need 4 KiB of payload and
100 ms of network time. A decoder stall alone cannot request a reduction.

The player selects the nearest strictly lower compatible bitrate and uses the same
clean teardown/requeue as manual quality selection. The existing state snapshot
preserves VOD position, pause, speed, audio delay and track choices, including the
existing unmatched-track fallback. Live playback restarts at the live edge. Channel
selection, paired-phone selection, parental limits and sleep remain authoritative.
The full play request retains provider credentials and HTTP headers.

A reduction starts a 20-second cooldown. Failed rendition bitrates and higher ones
are excluded for the rest of this Auto session, independent of master-list ordering.
The lower descriptor is revalidated against the next downloaded master. Pause,
seek, replay, quality and source changes discard old measurements; live replay
requires Go Live before automatic reduction resumes.

This is deliberately reduce-only, not a full adaptive bitrate algorithm. Declared
bitrates can be inaccurate; starvation is required in addition to a bitrate deficit.
Unknown media-resource signatures are not sampled. Synchronous HTTP calls remain
bounded by their existing transport timeout: Auto cannot interrupt a blocked socket
or promise immediate recovery. Hardware playback smoothness is not yet verified.

Host tests cover policy gating, cooldown/reset, failed ceilings and picker routing.
The ignored development fixture `workspace/dev/psiptv/.local/auto-quality-fixture.py`
extends the existing provider without changing its routes. `/auto/u/p/quality.m3u8`
uses the same synthetic fMP4 media at a paced 64 KiB/s connection: the high rendition
exceeds it and the low rendition fits. Keep User-Agent `QualityFixture`. Its sibling
`check-auto-quality-fixture.py` verifies matching range bytes and actual throttling
over loopback; it starts and stops its own server. No real account data is involved.
