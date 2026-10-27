# ProsperoTV - FW 3.x AGC/VideoDec2 startup regression
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Fw3AgcOrder(unittest.TestCase):
    def test_released_opengl_ui_orders_agc_before_compute(self):
        main = (ROOT / "opengl-ui/ps5/src/main.cpp").read_text()
        patch = (ROOT / "opengl-ui/ps5/patch_tree.py").read_text()
        entry = main[main.index("int main()"):]
        video = entry.index("iptv_native_backend_preload_video(decoders);")
        egl = entry.index("const bool opened = bootstrap.open();")
        compute = entry.index("iptv_native_backend_warm(decoders);")
        elevate = entry.index("tv::storage::initialize();")
        self.assertLess(video, egl)
        self.assertLess(egl, compute)
        self.assertLess(compute, elevate)
        self.assertIn("results[0] = sceSysmoduleLoadModule(VIDEO_MODULE_ID)", patch)
        self.assertIn("sceVideodec2QueryComputeMemoryInfo(&compute)", patch)
        self.assertIn("sceVideodec2QueryDecoderMemoryInfo(&config, &memory)", patch)
        self.assertIn("sceAudiodecInitLibrary(AUDIODEC_AAC)", patch)
        self.assertIn("void iptv_native_backend_preload_video", patch)
        self.assertIn("void iptv_native_backend_warm", patch)

    def test_production_workflow_uses_normal_title_and_logging(self):
        workflow = (ROOT / ".github/workflows/tooling.yml").read_text()
        self.assertNotIn("PPSA88003", workflow)
        self.assertNotIn("TV_TEST_TITLE:", workflow)
        self.assertNotIn("TV_DEBUG_TRACE:", workflow)
        self.assertNotIn("fix/fw3-agc-before-decoder-warmup", workflow)
        self.assertIn('run: HOST_CXX=clang++-18 bash opengl-ui/ps5/assemble.sh "$RUNNER_TEMP/app-build"', workflow)
        patch = (ROOT / "opengl-ui/ps5/patch_tree.py").read_text()
        self.assertIn('test_title = os.environ.get("TV_TEST_TITLE", "")', patch)
        self.assertIn('debug_trace = os.environ.get("TV_DEBUG_TRACE", "")', patch)


if __name__ == "__main__":
    unittest.main()
