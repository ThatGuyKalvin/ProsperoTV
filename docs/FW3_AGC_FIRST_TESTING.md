# AGC-first startup ordering for legacy PS5 firmware

## Problem and fix

FW 3.00 hardware tests showed ProsperoTV's normal startup could fail when
`sceVideodec2QueryComputeMemoryInfo` ran before the OpenGL runtime initialized
AGC. Related ProsperoLight testing observed NULL
`sceAgcGetRegisterDefaults` and a failed menu startup.

The released ProsperoTV OpenGL UI now follows this sequence:

1. Preload the VideoDec2 system module **before EGL and elevation**, retaining
   the system module-loader workaround.
2. Open and close a temporary `ps5::Display` to initialize AGC through the
   **same EGL/OpenGL runtime** used by the normal menu.
3. Query VideoDec2 compute, H.264, HEVC and VP9 decoder memory requirements,
   and warm AAC, all **after AGC** and **before filesystem elevation**.
4. Continue with the usual menu/player lifecycle. No special firmware flag,
   encoder setting, or graphics SDK fork is needed.

Do not call `sceAgcInit` directly in the app: the runtime has its own
initialization state and a different one-argument ABI from the older
two-argument native presenter's declaration. The first EGL bootstrap keeps
these consistent.

The fix is enabled in ordinary production builds. Previous test-only
workflow overrides for `TV_TEST_TITLE=PPSA88003` and `TV_DEBUG_TRACE=1`
were removed. `TV_TEST_TITLE` and `TV_DEBUG_TRACE` remain optional
pre-existing developer features in `patch_tree.py`, not new production
dependencies. CI builds its regular release title, `PPSA99003`.

## Validation

- FW 3.x: startup fix confirmed by tester; verify cold launch, menu and
  controller navigation, at least one H.264 playback session, returning to
  the menu and relaunching.
- Newer firmware: perform the same smoke test, optionally with HEVC/VP9,
  before the pull request is merged. The extra EGL open/close is universal
  and is not guarded by a firmware-version check.
- Review `[TV][FWCompat]` messages and, if a failure occurs, attach a fresh
  crash record plus the firmware and homebrew enabler versions.
- Because initialization happens before `tv::storage::initialize()`,
  early faults may require console/kernel debug output rather than
  the application's per-user logs.

The patch does not alter actual codec decoding, audio playback, VRR, or HFR
policy. The production candidate should be merged only after both legacy
and modern firmware smoke tests and full GitHub Actions validation.
