#!/usr/bin/env python3
# ProsperoTV - Edits that fit the copied sources and build script to this tree.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""usage: patch_tree.py <build tree>

Every edit names the exact text it replaces and fails when that text is not
found once, so a change in ProsperoTV or in the kit cannot be patched over
silently.
"""
import json
import os
import sys
from pathlib import Path

tree = Path(sys.argv[1])


def swap(path, old, new, count=1):
    file = tree / path
    text = file.read_text(encoding="utf-8")
    if text.count(old) != count:
        sys.exit(f"{path}: expected {count} of {old[:60]!r}, found {text.count(old)}")
    file.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


# ---- the keyboard: no SDL, and the controller is the menu's ----
swap("src/iptv_ime.c",
     '#include "iptv_input.h"\n\n#include <SDL2/SDL.h>\n#include <stddef.h>\n#include <stdint.h>\n',
     '#include <stddef.h>\n#include <stdint.h>\n#include <string.h>\n#include <time.h>\n\n'
     '/* The menu owns the controller: it says whether its confirm button is\n'
     ' * still down, so the press that asked for the keyboard is not typed. */\n'
     'extern bool iptv_ime_confirm_held(void);\n\n'
     'static void copy_text(char *output, const char *source, size_t capacity)\n'
     '{\n'
     '    size_t length = strlen(source);\n'
     '    if (capacity == 0)\n'
     '        return;\n'
     '    if (length >= capacity)\n'
     '        length = capacity - 1U;\n'
     '    memcpy(output, source, length);\n'
     '    output[length] = \'\\0\';\n'
     '}\n\n'
     'static uint32_t ticks_ms(void)\n'
     '{\n'
     '    struct timespec now = {0};\n'
     '    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)\n'
     '        return 0;\n'
     '    return (uint32_t)((uint64_t)now.tv_sec * UINT64_C(1000) +\n'
     '                      (uint64_t)now.tv_nsec / UINT64_C(1000000));\n'
     '}\n')
swap("src/iptv_ime.c",
     "    SDL_strlcpy(initial_text, value != NULL ? value : \"\", sizeof(initial_text));\n"
     "    SDL_strlcpy(requested_title, dialog_title != NULL ? dialog_title : \"\", sizeof(requested_title));\n"
     "    SDL_strlcpy(requested_placeholder, dialog_placeholder != NULL ? dialog_placeholder : \"\",\n"
     "                sizeof(requested_placeholder));\n",
     "    copy_text(initial_text, value != NULL ? value : \"\", sizeof(initial_text));\n"
     "    copy_text(requested_title, dialog_title != NULL ? dialog_title : \"\", sizeof(requested_title));\n"
     "    copy_text(requested_placeholder, dialog_placeholder != NULL ? dialog_placeholder : \"\",\n"
     "              sizeof(requested_placeholder));\n")
swap("src/iptv_ime.c", "    started_at = SDL_GetTicks();\n", "    started_at = ticks_ms();\n")
swap("src/iptv_ime.c",
     "    if (requested && !iptv_input_pressed(IPTV_INPUT_CROSS))\n",
     "    if (requested && !iptv_ime_confirm_held())\n")
swap("src/iptv_ime.c",
     "    if (status == 1 || (status == 0 && SDL_GetTicks() - started_at < 1000U))\n",
     "    if (status == 1 || (status == 0 && ticks_ms() - started_at < 1000U))\n")

# ---- the player's presenter: the menu's OpenGL runtime has already
#      initialised AGC in this process, and a second sceAgcInit says so
#      (0x8A6C0004 on hardware) without changing anything ----
swap("src/iptv_native_agc_present.c",
     "        result = sceAgcInit(&agc_state, 8);\n"
     "        if (result == 0)\n"
     "            agc_initialized = 1;\n",
     "        result = sceAgcInit(&agc_state, 8);\n"
     "        /* The menu's OpenGL runtime initialised AGC in this process first:\n"
     "         * the second call reports that and changes nothing. */\n"
     "        if ((uint32_t)result == UINT32_C(0x8A6C0004))\n"
     "            result = 0;\n"
     "        if (result == 0)\n"
     "            agc_initialized = 1;\n")

# ---- the player's messages ("IPTV: opening ...", errors, probes) went to the
#      console's notifications at the top left: the menu says what matters,
#      so they go to the log only ----
swap("src/iptv_player.cpp",
     "void Notify(const char *message)\n"
     "{\n"
     "    NotificationRequest request{};\n"
     "    if (message)\n"
     "    {\n"
     "        std::snprintf(request.message, sizeof(request.message), \"%s\", message);\n"
     "    }\n"
     "    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);\n"
     "}\n",
     "void Notify(const char *message)\n"
     "{\n"
     "    // Into the log, not onto the screen: the menu tells the viewer what\n"
     "    // went wrong when it comes back.\n"
     "    if (message)\n"
     "        std::fprintf(stdout, \"[ProsperoTV][player] %s\\n\", message);\n"
     "}\n")

# ---- the tuning screen (src/tv_tuning.cpp): the player's loading thread
#      shows the menu's picture with its bar moving, keeps it up while the
#      decoder opens, and lets it run out and fade before the first picture ----
swap("src/iptv_native_agc_present.h",
     "    int32_t iptv_native_agc_loading_start(void);\n"
     "    void iptv_native_agc_loading_stop(void);\n",
     "    int32_t iptv_native_agc_loading_start(void);\n"
     "    void iptv_native_agc_loading_stop(void);\n"
     "    /* The first picture is ready: the tuning screen finishes, fades out\n"
     "     * and gives the display up. Nothing happens without one. */\n"
     "    void iptv_native_agc_loading_finish(void);\n")
swap("src/iptv_native_agc_present.c",
     '#include "iptv_native_agc_present.h"\n',
     '#include "iptv_native_agc_present.h"\n#include "tv_tuning.h"\n')
swap("src/iptv_native_agc_present.c",
     "    if (!surface || loading.surface_bytes < LOADING_SURFACE_BYTES)\n"
     "        return -1;\n"
     "    memset(surface, 20, y_bytes);\n",
     "    if (!surface || loading.surface_bytes < LOADING_SURFACE_BYTES)\n"
     "        return -1;\n"
     "    if (tv_tuning_active())\n"
     "    {\n"
     "        /* The menu's tuning screen; 1 ends the thread once it has faded. */\n"
     "        if (!tv_tuning_compose(surface, loading.surface_bytes))\n"
     "            return 1;\n"
     "        flush_gpu_data(surface, LOADING_SURFACE_BYTES);\n"
     "        return iptv_native_agc_present_nv12(surface, loading.surface_bytes, LOADING_PITCH,\n"
     "                                            LOADING_SURFACE_HEIGHT, LOADING_PITCH,\n"
     "                                            LOADING_VISIBLE_HEIGHT, NULL);\n"
     "    }\n"
     "    memset(surface, 20, y_bytes);\n")
swap("src/iptv_native_agc_present.c",
     "        for (unsigned slice = 0; slice < 16u; ++slice)\n",
     "        /* The tuning screen moves every frame; presenting waits for the flip. */\n"
     "        for (unsigned slice = 0; !tv_tuning_active() && slice < 16u; ++slice)\n")
swap("src/iptv_native_agc_present.c",
     "    atomic_store_explicit(&present_cancelled, 0, memory_order_relaxed);\n"
     "    atomic_store_explicit(&loading.active, 1, memory_order_release);\n",
     "    atomic_store_explicit(&present_cancelled, 0, memory_order_relaxed);\n"
     "    tv_tuning_surface_changed();\n"
     "    atomic_store_explicit(&loading.active, 1, memory_order_release);\n")
swap("src/iptv_native_agc_present.c",
     "void iptv_native_agc_set_overlay_enabled(int enabled)\n",
     "void iptv_native_agc_loading_finish(void)\n"
     "{\n"
     "    void *thread_result = NULL;\n"
     "\n"
     "    if (!loading.thread || !tv_tuning_active())\n"
     "        return;\n"
     "    /* The loading thread runs the bar out and fades the screen, then ends. */\n"
     "    tv_tuning_finishing();\n"
     "    (void)scePthreadJoin(loading.thread, &thread_result);\n"
     "    loading.thread = NULL;\n"
     "    iptv_native_agc_loading_stop();\n"
     "    /* The video opens the display again at its own size. */\n"
     "    (void)teardown_presenter(1);\n"
     "}\n"
     "\n"
     "void iptv_native_agc_set_overlay_enabled(int enabled)\n")
swap("src/iptv_native_backend.c",
     "    started = monotonic_us();\n"
     "    state->telemetry.last_present_source = (uintptr_t)output->buffer;\n",
     "    /* The tuning screen leaves before the first picture is shown. */\n"
     "    iptv_native_agc_loading_finish();\n"
     "    started = monotonic_us();\n"
     "    state->telemetry.last_present_source = (uintptr_t)output->buffer;\n")
swap("src/iptv_player.cpp",
     "    iptv_native_agc_loading_stop();\n"
     "    const int handoff = iptv_native_agc_present_shutdown();\n"
     "    if (handoff != 0)\n"
     "        return handoff;\n"
     "    const int result = iptv_native_backend_open(&adapter->backend, &config);\n"
     "    adapter->opened = result == 0;\n"
     "    return result;\n",
     "    // With the tuning screen up, it stays while the decoder opens and leaves\n"
     "    // just before the first picture (iptv_native_agc_loading_finish).\n"
     "    if (tv_tuning_active())\n"
     "    {\n"
     "        tv_tuning_stage(0.55f, 0.80f);\n"
     "    }\n"
     "    else\n"
     "    {\n"
     "        iptv_native_agc_loading_stop();\n"
     "        const int handoff = iptv_native_agc_present_shutdown();\n"
     "        if (handoff != 0)\n"
     "            return handoff;\n"
     "    }\n"
     "    const int result = iptv_native_backend_open(&adapter->backend, &config);\n"
     "    adapter->opened = result == 0;\n"
     "    if (result == 0 && tv_tuning_active())\n"
     "        tv_tuning_stage(0.82f, 0.96f);\n"
     "    return result;\n")
swap("src/iptv_player.cpp",
     '#include "iptv_player.h"\n',
     '#include "iptv_player.h"\n#include "tv_tuning.h"\n')

# ---- build.sh: the audio decoders, and the system modules the public SDK
#      has no import library for ----
swap("tools/build.sh",
     'bash "$root/tools/setup-native-dependencies.sh" >/dev/null\n\nparam=',
     'bash "$root/tools/setup-native-dependencies.sh" >/dev/null\n'
     'bash "$root/tools/setup-audio-dependencies.sh"\n\nparam=')
swap("tools/build.sh",
     '# The OpenGL runtime needs the process-lifetime heap in src/runtime/app_heap.c.\n',
     '# Ordinary ELF facades for system modules the public SDK has no import\n'
     '# library for. They exist only for the linker and the module writer.\n'
     'mkdir -p "$build/import-stubs"\n'
     'system_stub() {\n'
     '    local library=$1 soname=$2 source=$3 standard=$4\n'
     '    local object="$build/import-stubs/$library.o" output="$build/import-stubs/$library.so"\n'
     '    PS5_PAYLOAD_SDK="$sdk_root" PS5_CLANG="$target_compiler" USE_CCACHE=0 \\\n'
     '        sh "$root/tooling/prospero-clang18" "$standard" -O2 -fPIC \\\n'
     '        -ffunction-sections -fdata-sections -c "$source" -o "$object"\n'
     '    "$sdk_root/bin/prospero-lld" --shared -soname "$soname" -o "$output" "$object"\n'
     '    stub_paths+=("$output")\n'
     '    stub_options+=(--stub "$output")\n'
     '}\n'
     'system_stub libSceVideodec2 libSceVideodec2.prx \\\n'
     '    "$root/vendor/ps5/sdk/stubs/videodec2_link_stub.c" -std=c11\n'
     'system_stub libSceCommonDialog libSceCommonDialog.sprx \\\n'
     '    "$native/ps5_radio_import_stub_common_dialog.cpp" -std=c++20\n'
     'system_stub libSceAudiodec libSceAudiodec.sprx \\\n'
     '    "$native/ps5_radio_import_stub_audiodec.cpp" -std=c++20\n'
     '# The OpenGL runtime needs the process-lifetime heap in src/runtime/app_heap.c.\n')

# ---- param.json. TV_CATEGORY picks the area the title shows in:
#   media (default)  as every release: Media, and no memory blocks
#   game             what every OpenGL title of the kit declares: Games, with
#                    the address-space and page-table blocks
param_path = tree / "sce_sys/param.json"
param = json.loads(param_path.read_text(encoding="utf-8"))
category = os.environ.get("TV_CATEGORY", "media")
if category == "game":
    param["applicationCategoryType"] = 0
    param["contentBadgeType"] = 1
    param["gameIntent"] = {"permittedIntents": [{"intentType": "launchActivity"}]}
    param["amm"] = {"multimapVaRangeInGib": 512, "pagetableMemorySizeInMib": 256,
                    "vaRangeInGib": 512}
    param["kernel"] = {"cpuPageTableSize": 268435456, "gpuPageTableSize": 268435456}
elif category != "media":
    sys.exit(f"TV_CATEGORY must be media or game, not {category!r}")

# ---- TV_TEST_TITLE=PPSA88nnn builds a disposable title for hardware checks:
#      it installs beside the released ProsperoTV, with storage of its own,
#      so a console test never touches the app the owner uses.
test_title = os.environ.get("TV_TEST_TITLE", "")
if test_title:
    import re

    if not re.fullmatch(r"PPSA88\d{3}", test_title):
        sys.exit("TV_TEST_TITLE must be PPSA88 followed by three digits")
    param["titleId"] = test_title
    param["conceptId"] = test_title[4:]
    param["contentId"] = f"UP9000-{test_title}_00-PROSPEROTVUITEST"
    for language in param["localizedParameters"].values():
        if isinstance(language, dict):
            language["titleName"] = "ProsperoTV UI test"
param_path.write_text(json.dumps(param, indent=2, sort_keys=True) + "\n", encoding="utf-8",
                      newline="\n")
# The test title also writes down what the video decoder asks of the system.
if test_title:
    import shutil

    shutil.copy(Path(__file__).resolve().parent / "diag/decoder_trace.c",
                tree / "src/runtime/decoder_trace.c")
    swap("tools/build.sh",
         '        wrap_options+=("--wrap=$symbol")\n    done\nfi\n',
         '        wrap_options+=("--wrap=$symbol")\n    done\nfi\n'
         'if [[ -f $root/src/runtime/decoder_trace.c ]]; then\n'
         '    for symbol in sceKernelAllocateDirectMemory sceKernelMapDirectMemory \\\n'
         '        sceKernelMapNamedFlexibleMemory sceVideodec2QueryComputeMemoryInfo \\\n'
         '        sceVideodec2AllocateComputeQueue sceVideodec2QueryDecoderMemoryInfo \\\n'
         '        sceVideodec2CreateDecoder sceSysmoduleLoadModule sceSysmoduleUnloadModule \\\n'
         '        sceAgcInit sceAgcCreateShader sceAgcLinkShaders sceAgcDriverSubmitDcb \\\n'
         '        sceAgcSuspendPoint sceVideoOutOpen sceVideoOutRegisterBuffers2 \\\n'
         '        sceAudiodecInitLibrary sceAudiodecCreateDecoder sceAudioOutInit sceAudioOutOpen; do\n'
         '        wrap_options+=("--wrap=$symbol")\n'
         '    done\n'
         'fi\n')
# The test title also reads scripted runs a PC leaves beside it.
(tree / "src/tv_build_options.h").write_text(
    "// ProsperoTV - What this build includes (written by ps5/patch_tree.py).\n#pragma once\n\n"
    f"#define TV_DEV_SCRIPTS {1 if test_title else 0}\n", encoding="utf-8", newline="\n")
print("tree patched, category", category, "title", param["titleId"])
