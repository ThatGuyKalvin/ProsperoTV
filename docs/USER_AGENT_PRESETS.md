# Source User-Agent presets

In Sources, select a saved source and press R2 (HTTP settings). Choose
IPTVSmartersPro, VLC, Default user agent, or Custom user agent. A preset changes
only that source's User-Agent; its credentials, referrer and extra headers stay
unchanged. Default clears the override. Custom retains the existing text editor
for all HTTP settings. Circle cancels without saving. Parental unlocking is
required, and changing an inactive source does not select it.

The compatibility tokens are `IPTVSmartersPro` and
`VLC/3.0.21 LibVLC/3.0.21`. These identify a compatibility preset, not the app's
implementation. VideoLAN's [3.0.21 source](https://github.com/videolan/vlc/blob/3.0.21/src/libvlc.c)
constructs the latter from its concrete package version. The requested
`IPTVSmartersPro` token is also documented by the public
[DashboardIPTV client](https://github.com/jaestradam/DashboardIPTV); it is not a
claim about the default header of every Smarters release.

Existing per-channel User-Agent overrides still take precedence. Source defaults
continue through playlist, guide, VOD and playback requests using the existing
header plumbing and redirect restrictions.

Host regressions cover selection, cancellation, persistence, validation, parental
locking, header preservation and download/playback propagation. Native console
acceptance remains pending an idle console.
