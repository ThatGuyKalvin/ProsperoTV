# Contributing

ProsperoTV is a native IPTV client for the PS5. The released interface lives in
[`opengl-ui/`](opengl-ui/README.md); the catalog, the stores, and the player it
uses are the shared sources in the rest of the repository. Keep a change small,
reproducible, and about one thing.

## Set up

On macOS, Windows or Linux, with Podman or Docker installed
([Development setup](docs/DEVELOPMENT.md) has the per-OS steps):

```bash
tools/dev.sh image    # once: the CI toolchain in a container
tools/dev.sh test     # the interface's PC tests
tools/dev.sh build    # the console app, in build/dev/PPSA99003
tools/dev.sh all      # build and deploy to the PS5 named by PS5_HOST in .env
tools/dev.sh preview  # PNGs of the interface in build/snapshots
```

On Windows, run `.\tools\dev.ps1` with the same commands.

## Before opening a change

1. Run `tools/dev.sh test`, `tools/dev.sh lint`, and `tools/dev.sh legacy-test`
   (natively: `opengl-ui/tools/run-tests.sh`, `make lint`, `make test`); add a
   focused unit or integration regression for behavior changed by the patch.
2. For a change to the released app, build it (`tools/dev.sh build`). For a
   change to the shared sources, `make` must reproduce the clean-room
   `runtime/libc.prx` digest.
3. Confirm the build reports zero static FSELF errors.
4. Do not commit `.env`, `build/`, `dist/`, `.local/`, proprietary PRXs, game files, SDK
   binaries, generated `runtime/libc.prx`, console dumps, keys, or credentials.
5. Include the firmware and loader context for platform-specific behavioral
   claims.
6. Name release tags with the exact `sce_sys/param.json` `contentVersion`
   (`NN.NNN.NNN`, without a `v` prefix).

Every comment-capable code, script, workflow, tooling configuration, and
manifest must carry a project copyright line, naming BlackBearReloaded or
ThatGuyKalvin, and the `GPL-3.0-or-later` SPDX header. Keep the copyright line a
file was created with. JSON and binary formats cannot carry comments;
their licensing is covered by `LICENSE` and `THIRD_PARTY_NOTICES.md`.

Changes to `tooling/native/` must include a deterministic host check and a
narrowly scoped static-format regression. Loader-visible changes also require
hardware results before release.
