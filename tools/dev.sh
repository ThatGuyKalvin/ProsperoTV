#!/usr/bin/env bash
# ProsperoTV - Build, test, preview and deploy in the development container, on any host.
# Copyright (C) 2026 ThatGuyKalvin
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/dev.sh <command> [arguments]      (docs/DEVELOPMENT.md)
#
#   image               build the development image (once, and after tools/dev/Containerfile changes)
#   doctor              check the container engine, the image, and the console's FTP server
#   test [gtest args]   the interface's PC tests (opengl-ui/tools/run-tests.sh)
#   build               assemble and build the app; the folder lands in build/dev/<TITLE_ID>
#   deploy [--full]     upload what changed since the last deploy to the PS5 (PS5_HOST)
#   all                 build, then deploy
#   preview [w h]       render the interface on the PC; the pictures land in build/snapshots
#   lint                make lint (format, headers, and clang-tidy on amd64 hosts)
#   legacy-test         make test (the shared sources' host tests)
#   shell               a shell in the container, in the synced tree
#
# CONTAINER   podman or docker (default: podman when installed, else docker)
# IMAGE       the image (default prosperotv-dev)
# VOLUME      the work volume: caches, builds, deploy manifests (default prosperotv-work)
# TREE        the app build tree in the volume (default /w/app-build)
# BUILD_JOBS  compile jobs (default: every CPU the container has)
# PS5_HOST, FTP_PORT  the console; read from .env when not set

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
command=${1:-help}
[[ $# -eq 0 ]] || shift

if [[ -z ${CONTAINER:-} ]]; then
    if command -v podman >/dev/null 2>&1; then
        CONTAINER=podman
    elif command -v docker >/dev/null 2>&1; then
        CONTAINER=docker
    else
        echo "install podman or Docker first (docs/DEVELOPMENT.md)" >&2
        exit 2
    fi
fi
IMAGE=${IMAGE:-prosperotv-dev}
VOLUME=${VOLUME:-prosperotv-work}

# The environment wins over .env.
env_value() {
    local name=$1
    if [[ -n ${!name:-} ]]; then
        printf '%s' "${!name}"
    elif [[ -f $root/.env ]]; then
        sed -n "s/^[[:space:]]*$name[[:space:]]*=[[:space:]]*//p" "$root/.env" | tail -1 | tr -d '\r"'"'"
    fi
}

run() {
    local tty=()
    [[ ! -t 0 || ! -t 1 ]] || tty=(-it)
    mkdir -p "$root/build"
    "$CONTAINER" run --rm ${tty[@]+"${tty[@]}"} \
        -v "$root":/src:ro -v "$root/build":/out -v "$VOLUME":/w \
        -e PS5_HOST="$(env_value PS5_HOST)" -e FTP_PORT="$(env_value FTP_PORT)" \
        -e BUILD_JOBS="$(env_value BUILD_JOBS)" -e TREE="${TREE:-/w/app-build}" \
        -e HOST_SANITIZE="${HOST_SANITIZE:-0}" -e LINT_SKIP_TIDY="${LINT_SKIP_TIDY:-}" \
        -e TV_LANGUAGE_OVERRIDE="${TV_LANGUAGE_OVERRIDE:-}" -e TV_SYSTEM_LANGUAGE="${TV_SYSTEM_LANGUAGE:-}" \
        "$IMAGE" bash /src/tools/dev/inside.sh "$@"
}

need_image() {
    "$CONTAINER" image inspect "$IMAGE" >/dev/null 2>&1 || {
        echo "no $IMAGE image yet: run tools/dev.sh image" >&2
        exit 2
    }
}

# The upload runs on the host when it has Python: a container engine's VM may not
# reach the local network (macOS asks before an app may), and the host always can.
deploy_app() {
    if command -v python3 >/dev/null 2>&1; then
        local id
        id=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/sce_sys/param.json")
        [[ -f $root/build/dev/$id/eboot.bin ]] || { echo "no build yet: run tools/dev.sh build" >&2; return 2; }
        PS5_HOST=$(env_value PS5_HOST) FTP_PORT=$(env_value FTP_PORT) \
            python3 "$root/tools/dev/ps5-deploy.py" "$root/build/dev/$id" --manifests "$root/.local/deploy" "$@"
    else
        need_image
        run deploy "$@"
    fi
}

timed() {
    local start=$SECONDS status=0
    "$@" || status=$?
    printf '==> %s: %d s\n' "$command" $((SECONDS - start))
    return "$status"
}

case $command in
    image)
        "$CONTAINER" build -t "$IMAGE" -f "$root/tools/dev/Containerfile" "$root/tools/dev"
        ;;
    doctor)
        "$CONTAINER" --version
        if "$CONTAINER" image inspect "$IMAGE" >/dev/null 2>&1; then
            echo "image: $IMAGE"
            run doctor
        else
            echo "image: missing (run tools/dev.sh image)"
        fi
        ;;
    test | build | preview | lint | legacy-test)
        need_image
        timed run "$command" "$@"
        ;;
    deploy)
        timed deploy_app "$@"
        ;;
    all)
        need_image
        command=build timed run build
        command=deploy timed deploy_app "$@"
        ;;
    shell)
        need_image
        run shell
        ;;
    help | -h | --help)
        sed -n '6,25p' "$0" | sed 's/^# \{0,1\}//'
        ;;
    *)
        echo "unknown command: $command (tools/dev.sh help)" >&2
        exit 2
        ;;
esac
