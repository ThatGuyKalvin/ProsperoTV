# Android mobile Premium feature checklist

Reference: the Android mobile Premium checklist supplied on 25 October 2026.
This document records ProsperoTV's implementation and remaining platform work.
It does not claim that every Smarters release has the same paid/free split.

| Requested capability | ProsperoTV status | Limits and evidence |
| --- | --- | --- |
| Master Search across live TV, movies and series | Already implemented | Search also includes programmes and cached saved sources, labels the provider, and rechecks hidden-content and parental restrictions before opening a result. An inactive source needs a saved catalogue. See `model_search.cpp`, `model_test.cpp`, and the merged PR #97. |
| Up to four channels side by side | Already implemented | Multiview supports two or four tiles, focus audio, mute and full-screen handoff. Historical hardware evidence covers four 720p H.264 streams, rather than every codec/resolution combination. See `multiview.cpp`, `app_test.cpp`, and `ROADMAP_REMAINING.md`. |
| Hardware acceleration and multiple internal engines | Native hardware player exists; additional engines remain open | H.264, HEVC and VP9 use PS5 decode paths. Android ExoPlayer and Android VLC integrations have not been ported. Adding a separate libVLC engine requires its own PS5 integration and validation. Changing a User-Agent preset does not select an engine. |
| Picture-in-picture while navigating | Implemented inside ProsperoTV; cross-app PiP remains open | Multiview's Touchpad action pins one channel while browsing the guide and other screens. A floating window over another PS5 application needs a verified system overlay interface; this change does not supply one. See `Multiview::draw_picture_in_picture` and the existing app regression. |
| EPG time shifting and automatic refresh | Automatic timezone parsing and scheduled refresh already implemented; per-source corrections added | The new Guide timing panel stores separate guide and catch-up offsets. It keeps cached programmes in original UTC and applies the guide offset on load and refresh. Provider-clock errors cannot be inferred automatically. |
| External-player routing | Open | No Android intent runtime exists in this PS5 build. The reviewed native VLC PS5 entry point is `main(void)` and its web server exposes file and subtitle operations, with no stream-play handoff endpoint. A receiving URL/header/authentication contract and launcher lifecycle must be implemented and tested before direct routing can be offered. |
| Ad-free interface | Already present | ProsperoTV has no developer banner-ad interface. This does not remove advertising embedded in provider streams. |

## Guide and catch-up corrections

In **Sources**, focus a saved source and press **R3** to open **Guide timing**.
Use Up/Down to choose the guide or catch-up correction. Left/Right changes it
in 30-minute steps, from minus 12 hours to plus 12 hours. Cross resets the
selected correction to zero; Circle returns to the source list. Editing is
parent-protected and waits until catalogue/guide downloads have finished.

A guide correction changes programme times consistently in the guide, Home,
search and playback banner. The guide cache remains in the provider's original
UTC values, so refreshing, reopening and changing corrections cannot accumulate
an offset. XMLTV's explicit timezone is still parsed before this correction.
Automatic guide refresh continues to follow the source's daily, weekly or
manual schedule; this change does not alter the console's clock or timezone.

Catch-up uses the provider's original archive time plus its own correction.
Changing the displayed guide time therefore does not silently change the
archive request. Archive eligibility follows the corrected displayed schedule,
while request timestamps use the independently corrected provider time. This
supports provider URL templates and Xtream archive URLs without changing
programme duration. Scheduled recordings/reminders keep the time at which they
were scheduled; reschedule an existing job if its provider timing was wrong.

Preferences are profile-local, separate for each saved source, and retained on
rename or schedule changes. Changing a source's address or account identity
clears its corrections to avoid carrying one provider's clock error to another.

## References

- [ProsperoTV search and browsing PR #97](https://github.com/ThatGuyKalvin/ProsperoTV/pull/97)
- [Smarters Pro Mobile Android listing](https://play.google.com/store/apps/details?id=com.smarters.smarterspro)
- [Smarters official Premium page](https://users.smarterspro.com/index.php?rp=%2Fstore%2Fpremium-version-smarters-pro)
- [Native VLC for PS5](https://github.com/ZiZc3/VLC), inspected at the repository's current main branch during this work

This change does not claim completion of additional-engine integration,
cross-application PiP or external-player routing. Those require separate
platform work and physical console validation.
