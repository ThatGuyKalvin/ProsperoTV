# macOS workstation setup and validation notes

This guide records a development workflow verified on an Apple silicon Mac
during review of [PR #96](https://github.com/ThatGuyKalvin/ProsperoTV/pull/96).
It supplements [Development setup](DEVELOPMENT.md) with a workstation baseline,
repeatable commands, measured results, and recovery steps. Measurements are
observations from this machine, not performance guarantees or minimum requirements.

Recorded: **2026-10-23**. Original reviewed commit:
`ea62fbab769ea3afefc93846c040b5cfcffcd12f`. The fixes accompanying this guide
address the arm64 test failures and incremental deployment findings described below.

## Verified workstation baseline

| Component | Observed configuration |
| --- | --- |
| Mac | MacBook Air, model identifier Mac14,2 |
| Processor | Apple M2, 8 cores: 4 performance and 4 efficiency |
| Physical memory | 8 GB |
| macOS | 26.6.2, build 25G83 |
| Shell used | zsh; project shell scripts run through Bash |
| Docker Engine client / server | 29.6.2 / 29.6.2, supplied by Docker Desktop |
| Docker Linux VM | aarch64, 8 CPUs, 4,108,632,064 bytes of memory (about 3.83 GiB) |
| Compile parallelism used | `BUILD_JOBS=2` |
| Container OS / compiler | Ubuntu 24.04 / Clang 18.1.3 |
| Container Python / Ninja / Make | 3.12.3 / 1.11.1 / GNU Make 4.3 |
| Host Git / Python | 2.55.0 / 3.15.0 |

Host Clang 23.1.3, Ninja 1.13.2, and ShellCheck 0.11.0 were also available.
The container workflow does not require those host compiler tools. Its Linux
compiler and libraries come from the development image.

The main image runs natively on arm64. Its build also runs a small amd64 stage
to obtain Clang's x86 runtime libraries for the PS5 target. That stage succeeded
on this Docker installation; the particular emulation setting was not inspected.

Device serial numbers, hardware UUIDs, private checkout paths, credentials, and
console addresses are intentionally absent from this public guide.

## First setup or reinstall

These instructions assume Homebrew is already installed. Install Git and
Docker Desktop if needed:

```bash
brew install git
brew install --cask docker
open -a Docker
```

Finish any first-run prompts in Docker Desktop. Wait until this command works:

```bash
docker info --format 'OS={{.OSType}} Architecture={{.Architecture}} CPUs={{.NCPU}} Memory={{.MemTotal}}'
```

On this 8 GB Mac, the reviewed configuration used approximately 4 GB for the
Docker VM and two compile jobs. Start with that combination. Avoid assigning
all physical memory to the VM. Larger configurations have not been measured
here. If a compiler is killed, reduce parallelism and inspect Docker's memory
settings before retrying.

Clone into a permanent folder of your choice, or use an existing checkout:

```bash
git clone https://github.com/ThatGuyKalvin/ProsperoTV.git
cd ProsperoTV
export CONTAINER=docker
export IMAGE=prosperotv-dev
export VOLUME=prosperotv-macos-work
export BUILD_JOBS=2
tools/dev.sh image
tools/dev.sh doctor
```

Use a revision containing these fixes when reproducing the complete arm64 test
workflow. The original PR commit alone fails two shared-code integration tests.
Building the image does not select or update the source branch for you.

The environment exports last for the current terminal session. Repeat them in
a new terminal before using the commands below. `CONTAINER=docker` avoids
accidentally selecting Podman if both engines are installed. Keep a consistent
volume name to retain dependency downloads and compiler caches.

## Daily workflow and output locations

Start Docker Desktop after a workstation shutdown:

```bash
open -a Docker
docker info
```

Then, from the checkout root with the environment above:

```bash
tools/dev.sh doctor
tools/dev.sh test
tools/dev.sh lint
tools/dev.sh legacy-test
tools/dev.sh build
tools/dev.sh preview
```

| Command | Purpose and output |
| --- | --- |
| `test` | Interface tests under AddressSanitizer and UBSan |
| `lint` | Source formatting, headers, metadata, shell syntax, and asset checks; clang-tidy is skipped on arm64 |
| `legacy-test` | Shared-code unit, tooling integration, media, and audio normalization tests |
| `build` | PS5 app folder in `build/dev/<TITLE_ID>/`; current title is PPSA99003 |
| `preview` | Scripted software-OpenGL screenshots in `build/snapshots/`; replaces the previous set |
| `shell` | Interactive shell in the container's synced source tree |

The checkout is mounted read-only at `/src`, except for its output directory,
which is mounted at `/out`. A copy under `/w/tv` and its dependencies/caches
live in the named Docker volume. The assembled console tree defaults to
`/w/app-build`. Container-only edits are temporary working state; make durable
source changes in the host checkout.

Rebuild the image after the Containerfile changes. Ordinary source edits need
only the relevant test or build command. `build` skips release ZIP generation;
the release workflow continues to build and validate the ZIP.

For simultaneous branches or checkouts, use a separate `VOLUME` for each.
Changing `TREE` alone does not isolate `/w/tv`, so do not run simultaneous
operations against the same volume. The review used separate test and console
build volumes for its concurrent runs.

## Console configuration and deployment

A physical console deployment was **not** performed during this review.
The following is the documented procedure, not a claim of console validation.
The PS5 must have its FTP server running, and the app must be closed.

Preserve an existing local configuration:

```bash
test -f .env || cp .env.example .env
```

Edit `.env` to set your own `PS5_HOST` and `FTP_PORT` (usually 2121). Keep the
local file out of commits. Build and deploy with:

```bash
tools/dev.sh build
tools/dev.sh doctor
tools/dev.sh deploy --full
tools/dev.sh all
```

Use `all` for later build-and-deploy iterations. `--full` forces every current
file to upload while retaining cleanup of obsolete files known to that
endpoint's manifest. It does not discover and remove arbitrary remote files.

With host Python 3 available, the wrapper uploads from the Mac, avoiding a
possible Docker-VM-to-LAN routing problem. Otherwise it attempts deployment
from the container. Host Python is optional for builds/tests; install it with
`brew install python` if host-side deployment is needed.

Host deployment inventories are under `.local/deploy/`. They are now scoped
by host, FTP port, and remote app folder. Old inventories without port identity
are ignored, causing a fresh upload. Files that only an old inventory knew
about are not automatically cleaned up during this transition.

FTP remains anonymous and unencrypted. A completed local file hash is not a
remote cryptographic integrity check. Replacement is not transactional: a
failure between target deletion and rename can leave a file missing. Keep the
app closed and retry a failed deployment; do not infer success from a partial
upload. Local manifest writes are also not atomic.

## Observed results and review findings

| Check | Recorded outcome |
| --- | --- |
| Development image | Built successfully, including the amd64 runtime stage |
| Cold interface tests | 321 tests in 50 suites passed; 379 seconds including preparation |
| Cold console app build | Passed in 357 seconds; clean-room runtime digest verified |
| No-change console rebuild | Passed in 8 seconds; Ninja reported no work to do |
| One-file console rebuild | Passed in 8 seconds; one source compiled, then relinked |
| Preview | Passed in 177 seconds; 81 PNGs, 5,914 simulated frames, logged GL errors 0x0 |
| Original shared-code command | 140 unit tests passed; 2 of 53 integration tests errored on x86 assembly |
| Fixed shared-code command | Passed in 65 seconds: 140 unit tests, 62 integration tests, 42 media tests, and audio checks |
| Added regression coverage | Nine tests passed on the Mac and in the arm64 container |
| Fixed lint / console rebuild | Passed; console rebuild took 9 seconds and runtime digest remained valid |

The tests exposed four issues corrected alongside this guide:

1. The arm64 host could not compile two x86-specific test fixtures. The jump
   harness now uses arm64 tail branches on arm64; GPU cache flushing is omitted
   only in non-x86 CPU-buffer overlay tests. The PS5 x86 path is unchanged.
2. `--full` discarded the prior inventory and forgot obsolete files. It now
   retains cleanup history while forcing uploads.
3. Permission-denied deletion responses were swallowed and forgotten. They
   now fail the operation and preserve inventory entries for retry.
4. Different FTP ports shared upload history. Endpoint identity now includes
   the port, with tests for isolation and old-inventory handling.

The nine regressions also cover interrupted upload/retry, already-missing
files, wiped remote folders, critical-file ordering, and timestamp preservation.
Deployment tests used an in-memory FTP implementation, not console hardware.
Windows and Podman execution were not validated on this workstation.

For reproducibility, the review image ID was
`sha256:8b674ac4a584507727c2ecf2b10deea5937bf5cf6cc31b80d08453b94620f39a`.
The Containerfile SHA-256 was
`032bfaacbcbe6f8cb038d112f2bf662bc8d4959f825bf84ec0eb1b27171af852`,
and the UI kit revision was `dbf8286661224c7d8b01fdea62052251eec43eca`.
An image ID identifies that local build; it is not a published image to pull.
Ubuntu tags and package updates can change subsequent image builds.

## Recovery and environment capture

- **Docker socket unavailable:** start Docker Desktop and wait for `docker info`
  to succeed. An installed Docker command alone does not mean the service is running.
- **Development image missing:** repeat the environment exports and run
  `tools/dev.sh image`.
- **Compiler killed or memory pressure:** retain two jobs or try one; avoid
  concurrent builds on this 8 GB machine. Inspect Docker's Resources settings.
- **Need to test a clean cache:** choose a new volume, for example
  `export VOLUME=prosperotv-macos-clean`, then rerun the command. Existing
  caches remain available under their previous volume names.
- **Need to remove a disposable volume:** stop its running containers first,
  inspect `docker volume ls`, and remove only the known disposable volume with
  `docker volume rm <volume-name>`. This deletes its cached dependencies,
  build trees, and any container-side deployment inventories.
- **Reinstalling or moving machines:** preserve the Git checkout and unpushed
  work, plus local `.env` and host deployment inventories in private storage.
  Reinstall Docker/Git, choose a new volume, rebuild the image, and rerun tests.
  Do not depend on the temporary review checkout or a container as the only copy.

Capture a fresh comparison baseline without device identifiers:

```bash
sw_vers
sysctl -n machdep.cpu.brand_string
sysctl -n hw.memsize
sysctl -n hw.ncpu
git --version
git rev-parse HEAD
docker version --format 'Client={{.Client.Version}} Server={{.Server.Version}}'
docker info --format 'OS={{.OSType}} Architecture={{.Architecture}} CPUs={{.NCPU}} Memory={{.MemTotal}}'
docker image inspect "$IMAGE" --format '{{.Id}}'
```

Record the revision, resource settings, compile jobs, exact command, exit
status, and whether its cache was cold or warm when comparing future runs.
Check outputs for private paths or connection details before publishing logs.
