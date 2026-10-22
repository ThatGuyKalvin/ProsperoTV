#!/usr/bin/env python3
# ProsperoTV - Upload a built app folder to the PS5 over FTP, sending only what changed.
# Copyright (C) 2026 ThatGuyKalvin
# SPDX-License-Identifier: GPL-3.0-or-later
"""usage: ps5-deploy.py <app folder> [--full] [--host H] [--port P] [--manifests DIR]

Uploads <app folder> to /data/homebrew/<its name> on the console. A manifest
of each file's sha256 per console and folder (in DIR, default .local/deploy)
records what was sent, so later runs upload only the files that changed and
delete the ones the build no longer has. Each file is uploaded beside its
target and renamed into place; sce_sys/param.json and eboot.bin go last.

--full uploads everything (after the folder on the console was removed);
files the build no longer has are still deleted. The host defaults to PS5_HOST and the port to FTP_PORT
or 2121. The console's FTP server must be running and the app closed.
"""
import argparse
import ftplib
import hashlib
import json
import os
import sys
import time

parser = argparse.ArgumentParser(usage=__doc__.split("\n")[0][7:])
parser.add_argument("folder")
parser.add_argument("--full", action="store_true")
parser.add_argument("--host", default=os.environ.get("PS5_HOST", ""))
parser.add_argument("--port", type=int, default=int(os.environ.get("FTP_PORT") or 2121))
parser.add_argument("--manifests", default=os.path.join(".local", "deploy"))
args = parser.parse_args()
if not args.host:
    sys.exit("set PS5_HOST (in .env or the environment) or pass --host")

dist = os.path.abspath(args.folder)
remote_root = "/data/homebrew/" + os.path.basename(dist.rstrip("/"))
# The FTP server hands back decrypted copies of these, so their sizes differ.
SELF = {"eboot.bin", "libc.prx"}
os.makedirs(args.manifests, exist_ok=True)
manifest_path = os.path.join(args.manifests, f"{args.host}-{args.port}-{os.path.basename(remote_root)}.json")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


local = {}
for root, _dirs, files in os.walk(dist):
    for name in files:
        path = os.path.join(root, name)
        local[os.path.relpath(path, dist).replace(os.sep, "/")] = path
if "eboot.bin" not in local:
    sys.exit(f"{dist}: no eboot.bin")

known = {}
if os.path.exists(manifest_path):
    with open(manifest_path, encoding="utf-8") as fh:
        known = json.load(fh)

hashes = {rel: sha256(path) for rel, path in local.items()}
changed = [rel for rel in local if args.full or known.get(rel) != hashes[rel]]
gone = [rel for rel in known if rel not in local]


def last(rel):
    return (rel == "eboot.bin", rel == "sce_sys/param.json")


changed.sort(key=lambda rel: (last(rel), rel))

f = ftplib.FTP()
try:
    f.connect(args.host, args.port, timeout=60)
except OSError as error:
    sys.exit(f"no FTP server at {args.host}:{args.port} ({error})")
f.login()
f.voidcmd("TYPE I")

try:
    f.cwd(remote_root)
except ftplib.error_perm:
    if known:
        print(f"{remote_root} is missing on the console: uploading everything")
        changed = sorted(local, key=lambda rel: (last(rel), rel))
        gone = []
        known = {}
f.cwd("/")

made = set()


def ensure_dir(path):
    if path in made:
        return
    parent = path.rsplit("/", 1)[0]
    if parent and parent != "/data":
        ensure_dir(parent)
    try:
        f.mkd(path)
    except ftplib.error_perm:
        pass
    made.add(path)


def save():
    with open(manifest_path, "w", encoding="utf-8") as fh:
        json.dump(known, fh, indent=1, sort_keys=True)


def delete(path):
    """DELE accepting any 2xx reply (the FTP server answers 226, not 250); a missing file is fine."""
    try:
        reply = f.sendcmd("DELE " + path)
    except ftplib.error_perm as error:
        if "no such" in str(error).lower() or "not found" in str(error).lower():
            return
        raise
    if not reply.startswith("2"):
        raise ftplib.error_reply(reply)


def rename(source, target):
    reply = f.sendcmd("RNFR " + source)
    if reply[0] not in "23":
        raise ftplib.error_reply(reply)
    reply = f.sendcmd("RNTO " + target)
    if not reply.startswith("2"):
        raise ftplib.error_reply(reply)


ensure_dir(remote_root)
if not changed and not gone:
    print("nothing changed since the last deploy")
    f.quit()
    sys.exit(0)

total = sum(os.path.getsize(local[rel]) for rel in changed)
print(f"uploading {len(changed)} file(s), {total / 1e6:.1f} MB; deleting {len(gone)}")
t0 = time.time()
sent = 0

for rel in gone:
    delete(f"{remote_root}/{rel}")
    del known[rel]
save()

for rel in changed:
    path = local[rel]
    target = f"{remote_root}/{rel}"
    directory, name = target.rsplit("/", 1)
    ensure_dir(directory)
    temp = f"{directory}/.{name}.upload"
    with open(path, "rb") as fh:
        f.storbinary("STOR " + temp, fh, blocksize=1 << 18)
    if name not in SELF:
        size = f.size(temp)
        if size != os.path.getsize(path):
            delete(temp)
            sys.exit(f"{rel}: size mismatch after upload ({size} != {os.path.getsize(path)})")
    delete(target)
    rename(temp, target)
    known[rel] = hashes[rel]
    save()
    sent += os.path.getsize(path)
    print(f"  {rel} ({os.path.getsize(path) / 1e6:.1f} MB)")

elapsed = time.time() - t0
print(f"deployed {sent / 1e6:.1f} MB in {elapsed:.1f} s ({sent / 1e6 / max(elapsed, 0.001):.1f} MB/s)")
f.quit()
