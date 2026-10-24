# Development setup

The released app builds in a Linux container with the same Ubuntu 24.04
toolchain CI uses. `tools/dev.sh` (macOS, Linux) and `tools/dev.ps1`
(Windows PowerShell) run every step in it: the PC tests, the console build, the
PC preview, and the deploy to a PS5. Nothing is installed on the host besides a
container engine and Git.

## 1. Install a container engine and Git

**macOS** (Homebrew):

```bash
brew install git podman
podman machine init --cpus 8 --memory 8192 --disk-size 100
podman machine start
```

Docker Desktop works too (`brew install --cask docker`). On Apple silicon, turn
on **Use Rosetta for x86_64/amd64 emulation** in its settings: building the
image runs one small amd64 step.

**Windows** (PowerShell):

```powershell
winget install Git.Git Docker.DockerDesktop
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

Start Docker Desktop once and let it finish setting up. Podman Desktop
(`winget install RedHat.Podman-Desktop`) works too. Clone anywhere;
`.gitattributes` keeps the scripts' line endings right.

**Linux**: install `podman` or `docker` from your distribution. To build
without a container, see [Native Linux](#native-linux).

## 2. First run

```bash
git clone https://github.com/ThatGuyKalvin/ProsperoTV.git
cd ProsperoTV
tools/dev.sh image     # build the development image (a few minutes, once)
tools/dev.sh doctor    # engine, image, and the console's FTP server
tools/dev.sh test      # the interface's PC tests
```

On Windows, use `.\tools\dev.ps1` in place of `tools/dev.sh`; the commands
are the same.

The first `test` fetches and builds the test dependencies into the
`prosperotv-work` volume (about 6 minutes). Later runs recompile only the
files that changed. Pass GoogleTest arguments after `test`, for example
`tools/dev.sh test --gtest_filter='Model*'`.

## 3. Build and deploy to a PS5

The console needs ftpsrv running (port 2121 by default), and the app must be
closed while files are replaced.

```bash
cp .env.example .env     # then set PS5_HOST to the console's address
tools/dev.sh build       # the app folder lands in build/dev/PPSA99003
tools/dev.sh deploy --full   # the first time: upload everything
tools/dev.sh all         # afterwards: build, then upload only what changed
```

The first build fetches the SDKs and builds the dependencies (about 15
minutes); after that a one-line change builds in about 5 seconds. `deploy` keeps a
hash of every file it sent, so it uploads only what changed (usually
`eboot.bin`). Use `--full` again after the folder on the console was removed.
`build` leaves out the release ZIP; CI builds that.

## 4. Preview the interface on the PC

```bash
tools/dev.sh preview              # pictures of a scripted walk through the interface
tools/dev.sh preview 3840 2160    # at another size
HOST_SANITIZE=1 tools/dev.sh preview
TV_LANGUAGE_OVERRIDE=8 tools/dev.sh preview   # Arabic (docs/RTL_TEXT.md)
```

It renders with Mesa's software OpenGL and writes PNG files to
`build/snapshots/`, replacing the previous set. The first run builds for about
3 minutes.

## 5. Other commands

| Command | Does |
| --- | --- |
| `tools/dev.sh lint` | `make lint`: format and file headers, plus clang-tidy on amd64 hosts (`LINT_SKIP_TIDY=1` skips it) |
| `tools/dev.sh legacy-test` | `make test`: the shared sources' host tests |
| `tools/dev.sh shell` | a shell in the container, in the synced tree (`/w/tv`) |
| `tools/dev.sh help` | every command and variable |

Every command first copies the checkout into the volume, so the volume holds
the caches and builds and the checkout stays clean. Several worktrees share
one volume and one build tree; give a worktree its own with
`TREE=/w/app-build-<name>`, or its own volume with `VOLUME=`. `CONTAINER=docker`
picks Docker when both engines are installed. `podman volume rm
prosperotv-work` (or `docker volume rm`) starts afresh.

`deploy` runs on the host when it has Python 3 (the container engine's VM may
not reach the local network), else in the container. By hand:
`python3 tools/dev/ps5-deploy.py build/dev/PPSA99003 --host <address>`.

## Native Linux

On Ubuntu 24.04 (or WSL), install the packages CI installs:

```bash
sudo apt install clang-18 clang-format-18 clang-tidy-18 lld-18 llvm-18 \
  make ninja-build meson cmake ccache pkg-config python3 git curl wget tar unzip \
  xz-utils bzip2 ffmpeg libsqlite3-dev libpng-dev libjpeg-dev zlib1g-dev \
  libssl-dev libavcodec-dev libswresample-dev libegl-dev libgl-dev libgl1-mesa-dri
```

Then, from the repository root:

```bash
HOST_CXX=clang++-18 opengl-ui/tools/run-tests.sh
HOST_CXX=clang++-18 opengl-ui/tools/host-snapshots.sh
opengl-ui/ps5/assemble.sh             # the build tree: ../prosperotv-ui-build
make -C ../prosperotv-ui-build app    # dist/PPSA99003 and its ZIP
python3 tools/dev/ps5-deploy.py ../prosperotv-ui-build/dist/PPSA99003 --host <address>
```

## Troubleshooting

- **`no prosperotv-dev image yet`**: run `tools/dev.sh image`, and again after
  `tools/dev/Containerfile` changes.
- **The build is killed, or the compiler crashes**: the engine's VM has too
  little memory. Give it at least 8 GB
  (`podman machine set --memory 8192`, or Docker Desktop's Resources).
- **`no FTP server at …`**: the console is off, ftpsrv isn't running,
  or `PS5_HOST` is wrong. `tools/dev.sh doctor` checks it.
- **A deploy fails part-way**: close the app on the console and run it again;
  files already sent are not sent twice.
- **`$'\r': command not found`**: a script was checked out with Windows line
  endings. Clone again, or run `git rm -r -q --cached . && git reset --hard`
  (this discards uncommitted changes).
