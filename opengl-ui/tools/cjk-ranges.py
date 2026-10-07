#!/usr/bin/env python3
# ProsperoTV - The code points the Chinese, Japanese and Korean faces are baked with.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the code point lists the CJK faces are baked with.

usage: tools/cjk-ranges.py <out dir>

east-asian.txt (for Noto Sans SC): the characters of GB 2312 (Simplified Chinese), of Big5 (Traditional
Chinese) and of JIS X 0208 (Japanese: kana and kanji), with CJK punctuation and the full-width forms.
korean.txt (for Noto Sans KR): the Hangul of KS X 1001 and the compatibility jamo.
Both carry printable ASCII, so a name that mixes "CCTV-5" with Chinese is drawn in one face.
The sets come from Python's own codecs, so the lists are the same wherever they are made.
"""
import sys
from pathlib import Path

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)


def encodable(codec, low, high):
    found = set()
    for c in range(low, high + 1):
        try:
            chr(c).encode(codec)
            found.add(c)
        except UnicodeEncodeError:
            pass
    return found


def write(name, title, codepoints):
    points = sorted(codepoints)
    lines = [f"# {title}", f"# {len(points)} code points"]
    start = previous = points[0]
    for c in points[1:] + [None]:
        if c is not None and c == previous + 1:
            previous = c
            continue
        lines.append(f"{start:04x}" if start == previous else f"{start:04x}-{previous:04x}")
        start = previous = c
    (out / name).write_text("\n".join(lines) + "\n")
    print(name, len(points), "code points")


ascii_printable = set(range(0x20, 0x7f))
ideographs = set()
for codec in ("gb2312", "big5", "shift_jis"):
    ideographs |= encodable(codec, 0x3400, 0x9fff)
east = ascii_printable | ideographs
east |= set(range(0x3000, 0x3040))  # CJK symbols and punctuation
east |= set(range(0x3040, 0x3100))  # hiragana, katakana
east |= set(range(0xff00, 0xfff0))  # full-width and half-width forms
east |= {0x00b7, 0x2014, 0x2018, 0x2019, 0x201c, 0x201d, 0x2026, 0x2022}
write("east-asian.txt", "GB 2312, Big5 and JIS X 0208 characters, kana, CJK punctuation, full-width forms, ASCII", east)

korean = ascii_printable | encodable("euc_kr", 0xac00, 0xd7a3) | set(range(0x3130, 0x3190))
korean |= {0x00b7, 0x2026}
write("korean.txt", "KS X 1001 Hangul, compatibility jamo, ASCII", korean)
