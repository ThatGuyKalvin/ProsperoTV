#!/usr/bin/env bash
# ProsperoTV - The steps tools/dev.sh runs inside the development container.
# Copyright (C) 2026 ThatGuyKalvin
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: inside.sh sync|test|build|preview|lint|legacy-test|deploy|doctor|shell [arguments]
#
# /src  the checkout (read-only)
# /out  the checkout's build/ folder (outputs are copied here)
# /w    the work volume: the synced tree, the app build tree, the caches
#
# TREE        the app build tree (default /w/app-build)
# BUILD_JOBS  ninja/make jobs (default: every CPU)
# PS5_HOST, FTP_PORT  the console, for deploy and doctor

set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
tv=/w/tv
TREE=${TREE:-/w/app-build}
BUILD_JOBS=${BUILD_JOBS:-$(nproc)}
export BUILD_JOBS
mkdir -p "$CCACHE_DIR"
ccache -M 5G >/dev/null

# Copy the checkout into the volume. -c compares content, so unchanged files keep
# their modification times and ninja recompiles only what changed. The caches
# and builds in the volume are excluded, so --delete leaves them alone.
sync_tree() {
    mkdir -p "$tv"
    rsync -rlpc --delete --exclude=.git --exclude=/.deps --exclude=/build --exclude=/dist \
        --exclude=/.local --exclude=/external_apps --exclude=/opengl-ui/.deps \
        --exclude=/opengl-ui/build /src/ "$tv"/
    # A checkout on a Windows drive can lose the executable bits Git recorded.
    if git -C /src rev-parse --git-dir >/dev/null 2>&1; then
        git -C /src ls-files -s | awk '$1 == "100755" { sub(/^[^\t]*\t/, ""); print }' |
            while IFS= read -r path; do
                [[ ! -f $tv/$path ]] || chmod +x "$tv/$path"
            done
    fi
    # The lint lists files with Git; an empty repository lists every file not ignored.
    [[ -d $tv/.git ]] || git -C "$tv" init -q
}

title_id() {
    python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["titleId"])' /src/sce_sys/param.json
}

step=${1:?step}
shift
case $step in
    sync)
        sync_tree
        ;;
    test)
        sync_tree
        HOST_CXX=clang++-18 bash "$tv/opengl-ui/tools/run-tests.sh" "$@"
        ;;
    build)
        sync_tree
        mkdir -p "$TREE"
        bash "$tv/opengl-ui/ps5/assemble.sh" "$TREE"
        PTV_SKIP_ZIP=1 make -C "$TREE" app
        id=$(title_id)
        mkdir -p /out/dev
        rsync -rlc --delete "$TREE/dist/$id/" "/out/dev/$id/"
        printf 'App folder: build/dev/%s\n' "$id"
        ;;
    preview)
        sync_tree
        rm -rf "$tv/opengl-ui/build/snapshots"
        HOST_CXX=clang++-18 bash "$tv/opengl-ui/tools/host-snapshots.sh" "$tv/opengl-ui/build/snapshots" "$@"
        mkdir -p /out/snapshots
        rsync -r --delete "$tv/opengl-ui/build/snapshots/" /out/snapshots/
        printf 'Pictures: build/snapshots (%s files)\n' "$(find /out/snapshots -name '*.png' | wc -l)"
        ;;
    lint)
        sync_tree
        # clang-tidy reads the host sources as host code, and they use x86 intrinsics.
        if [[ -z ${LINT_SKIP_TIDY:-} && $(uname -m) != x86_64 ]]; then
            echo "clang-tidy skipped: it needs an x86_64 container (CI skips it too)"
            export LINT_SKIP_TIDY=1
        fi
        make -C "$tv" lint
        ;;
    legacy-test)
        sync_tree
        make -C "$tv" test
        ;;
    deploy)
        id=$(title_id)
        [[ -f $TREE/dist/$id/eboot.bin ]] || { echo "no build yet: run tools/dev.sh build" >&2; exit 2; }
        python3 "$here/ps5-deploy.py" "$TREE/dist/$id" --manifests /w/deploy "$@"
        ;;
    doctor)
        printf 'container: %s, %s CPUs, %s\n' "$(uname -m)" "$(nproc)" "$(clang-18 --version | head -1)"
        if [[ -n ${PS5_HOST:-} ]]; then
            if python3 -c 'import socket, sys; socket.create_connection((sys.argv[1], int(sys.argv[2])), 3).close()' \
                "$PS5_HOST" "${FTP_PORT:-2121}" 2>/dev/null; then
                echo "console: FTP answers at $PS5_HOST:${FTP_PORT:-2121}"
            else
                echo "console: no FTP at $PS5_HOST:${FTP_PORT:-2121} (is the console on, with ftpsrv running?)"
            fi
        else
            echo "console: PS5_HOST is not set (in .env or the environment); needed only to deploy"
        fi
        ;;
    shell)
        sync_tree
        cd "$tv"
        exec bash
        ;;
    *)
        echo "unknown step: $step" >&2
        exit 2
        ;;
esac
