#!/usr/bin/env bash
# ProsperoTV - Isolated host crash checks; intentionally not sanitizer-instrumented.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/prosperotv-crash-build-XXXXXX")
trap 'rm -rf -- "$build"' EXIT
# Do not inherit CXXFLAGS: sanitizer-installed signal handlers would change this test.
"${CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
    -I"$root/src" "$root/src/tv/crash_report.cpp" \
    "$root/tests/crash_report_check.cc" -o "$build/crash-report-check"
"$build/crash-report-check"
