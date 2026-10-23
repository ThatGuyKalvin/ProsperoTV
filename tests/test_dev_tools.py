#!/usr/bin/env python3
# ProsperoTV - Development deploy and incremental assembly regressions.
# Copyright (C) 2026 ThatGuyKalvin
# SPDX-License-Identifier: GPL-3.0-or-later

import contextlib
import ftplib
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


class MemoryFTP:
    """Deterministic FTP responses without a network or console."""

    def __init__(self):
        self.root = "/data/homebrew/PPSA99003"
        self.files = {}
        self.directories = {"/", "/data", "/data/homebrew", self.root}
        self.uploads = []
        self.denied_deletes = set()
        self.failed_uploads = set()

    def connect(self, host, port, timeout):
        pass

    def login(self):
        pass

    def voidcmd(self, command):
        return "200 OK"

    def cwd(self, path):
        if path not in self.directories:
            raise ftplib.error_perm("550 Not found")

    def mkd(self, path):
        self.directories.add(path)

    def storbinary(self, command, source, blocksize):
        path = command.removeprefix("STOR ")
        if path in self.failed_uploads:
            raise OSError("connection lost during upload")
        self.files[path] = source.read()
        self.uploads.append(path)

    def size(self, path):
        return len(self.files[path])

    def sendcmd(self, command):
        action, path = command.split(" ", 1)
        if action == "DELE":
            if path in self.denied_deletes:
                raise ftplib.error_perm("550 Permission denied")
            if path not in self.files:
                raise ftplib.error_perm("550 No such file")
            del self.files[path]
            return "226 Deleted"
        if action == "RNFR":
            self.rename_source = path
            return "350 Ready"
        if action == "RNTO":
            self.files[path] = self.files.pop(self.rename_source)
            return "250 Renamed"
        raise AssertionError(command)

    def quit(self):
        pass


class DeployTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name) / "PPSA99003"
        self.folder.mkdir()
        self.manifests = Path(self.temp.name) / "manifests"
        self.ftp = MemoryFTP()
        self.write("eboot.bin", b"executable")
        self.write("sce_sys/param.json", b"{}")

    def write(self, relative, content):
        path = self.folder / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def deploy(self, *arguments, ftp=None):
        script = ROOT / "tools/dev/ps5-deploy.py"
        argv = [str(script), str(self.folder), "--host", "console", "--port", "2121",
                "--manifests", str(self.manifests), *arguments]
        with patch.object(sys, "argv", argv), \
                patch.object(ftplib, "FTP", return_value=ftp or self.ftp), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            try:
                runpy.run_path(str(script), run_name="__main__")
            except SystemExit as error:
                if error.code not in (None, 0):
                    raise
            return output.getvalue()

    def test_unchanged_deploy_skips_uploads_and_critical_files_are_last(self):
        self.write("asset.dat", b"asset")
        self.deploy()
        self.assertEqual([path.rsplit("/", 1)[-1] for path in self.ftp.uploads],
                         [".asset.dat.upload", ".param.json.upload", ".eboot.bin.upload"])
        self.ftp.uploads.clear()
        self.assertIn("nothing changed", self.deploy())
        self.assertEqual(self.ftp.uploads, [])

    def test_full_deploy_removes_obsolete_files_and_uploads_unchanged_files(self):
        self.write("obsolete.dat", b"obsolete")
        self.deploy()
        (self.folder / "obsolete.dat").unlink()
        self.ftp.uploads.clear()
        self.deploy("--full")
        self.assertNotIn(self.ftp.root + "/obsolete.dat", self.ftp.files)
        self.assertEqual(len(self.ftp.uploads), 2)
        self.assertIn("nothing changed", self.deploy())

    def test_denied_deletion_fails_and_is_retried(self):
        self.write("obsolete.dat", b"obsolete")
        self.deploy()
        (self.folder / "obsolete.dat").unlink()
        remote = self.ftp.root + "/obsolete.dat"
        self.ftp.denied_deletes.add(remote)
        with self.assertRaisesRegex(ftplib.error_perm, "Permission denied"):
            self.deploy()
        manifest = next(self.manifests.glob("*.json"))
        self.assertIn("obsolete.dat", json.loads(manifest.read_text()))
        self.ftp.denied_deletes.clear()
        self.deploy()
        self.assertNotIn(remote, self.ftp.files)

    def test_already_missing_obsolete_file_is_harmless(self):
        self.write("obsolete.dat", b"obsolete")
        self.deploy()
        (self.folder / "obsolete.dat").unlink()
        del self.ftp.files[self.ftp.root + "/obsolete.dat"]
        self.deploy()
        self.assertIn("nothing changed", self.deploy())

    def test_different_port_does_not_reuse_upload_history(self):
        self.deploy()
        second = MemoryFTP()
        second.files[second.root + "/eboot.bin"] = b"older executable"
        self.deploy("--port", "2222", ftp=second)
        self.assertEqual(second.files[second.root + "/eboot.bin"], b"executable")
        self.assertEqual(len(list(self.manifests.glob("*.json"))), 2)
        self.ftp.uploads.clear()
        self.assertIn("nothing changed", self.deploy())
        self.assertEqual(self.ftp.uploads, [])

    def test_old_unscoped_manifest_cannot_suppress_first_upload(self):
        self.manifests.mkdir()
        old = {path.relative_to(self.folder).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
               for path in self.folder.rglob("*") if path.is_file()}
        (self.manifests / "console-PPSA99003.json").write_text(json.dumps(old))
        self.deploy()
        self.assertEqual(self.ftp.files[self.ftp.root + "/eboot.bin"], b"executable")

    def test_failed_upload_keeps_old_executable_and_retry_succeeds(self):
        self.deploy()
        self.write("eboot.bin", b"new executable")
        temporary = self.ftp.root + "/.eboot.bin.upload"
        self.ftp.failed_uploads.add(temporary)
        with self.assertRaisesRegex(OSError, "connection lost"):
            self.deploy()
        self.assertEqual(self.ftp.files[self.ftp.root + "/eboot.bin"], b"executable")
        self.ftp.failed_uploads.clear()
        self.deploy()
        self.assertEqual(self.ftp.files[self.ftp.root + "/eboot.bin"], b"new executable")

    def test_wiped_remote_folder_forces_complete_upload(self):
        self.deploy()
        self.ftp.files.clear()
        self.ftp.directories.remove(self.ftp.root)
        self.deploy()
        self.assertEqual(self.ftp.files[self.ftp.root + "/eboot.bin"], b"executable")
        self.assertEqual(self.ftp.files[self.ftp.root + "/sce_sys/param.json"], b"{}")


class AssemblyTimestampTests(unittest.TestCase):
    def test_only_unchanged_files_recover_their_timestamps(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            (tree / "src").mkdir()
            stable, changed = tree / "src/stable.cpp", tree / "src/changed.cpp"
            stable.write_text("stable")
            changed.write_text("before")
            old = 1_000_000_000_000_000_000
            for path in (stable, changed):
                os.utime(path, ns=(old, old))
            script = ROOT / "opengl-ui/ps5/keep_mtimes.py"
            subprocess.run([sys.executable, str(script), "snapshot", directory], check=True)
            stable.write_text("stable")
            changed.write_text("after")
            subprocess.run([sys.executable, str(script), "restore", directory], check=True)
            self.assertEqual(stable.stat().st_mtime_ns, old)
            self.assertNotEqual(changed.stat().st_mtime_ns, old)
            self.assertFalse((tree / ".keep-mtimes.json").exists())


if __name__ == "__main__":
    unittest.main()
