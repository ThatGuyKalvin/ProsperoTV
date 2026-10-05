# Filesystem access with upstream Lapy

ProsperoTV uses [PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon),
created by ArkSama, and pins the cooperative owned-root implementation from
[mpereiraesaa's Lapy fork](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon). The build
(`tools/build-lapy-helper.py`, run by `tools/package-extras.sh`) fetches that pinned commit and
invokes upstream's own `owned-helper` target for the title being built; it does not copy or
modify Lapy's privileged source. Before packaging, it verifies the generated manifest's title,
mode, ELF and protocol hashes, and `root_layout_probe_retry` feature.

The three files here are the PS5 Native App Boilerplate's client
(`examples/sandbox-elevation`), as ProsperoEden uses it. `tv_storage.cpp` calls it once, first
thing in `main`, while the process has a single thread:

1. If `/data` can already be written, nothing is asked for.
2. The client publishes the cooperative resident-service request through
   `/download0/elevate_proc` and waits 1.5 seconds for verified `/data` read/write access. If a
   resident service claimed the marker, the one-shot path is not started.
3. Otherwise it cancels the marker, observes one further grace interval, and sends
   `/app0/lapy.elf` to the local ELF loader on TCP port 9021. The same connection carries
   upstream's fixed-width request/prepare/prepared/response exchange.
4. Only a successful response followed by an actual `/data` write/read/delete probe counts.

With access the app keeps its files in `/data/prosperotv` and reads its own folder from where
the console mounts it; without it (no resident service and no loader, or a refusal) it stays in
its sandbox, `/app0` and `/download0`, exactly as the released app does. `tv_storage.hpp` says
which paths those are.

The package includes `lapy.elf`, `lapy-manifest.json`, and Lapy's MIT license
(`licenses/Lapy-MIT.txt`). It contains no locally implemented kernel mutation code. A jailbreak
environment with an ELF loader on port 9021 is required when a resident Lapy service is not
already available.

Credits: Lapy was created by [ArkSama](https://github.com/ArkSama), and the cooperative helper
used here comes from
[mpereiraesaa and contributors](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon/graphs/contributors).
