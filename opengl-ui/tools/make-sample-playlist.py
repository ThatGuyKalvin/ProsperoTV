#!/usr/bin/env python3
# ProsperoTV - Writes an invented playlist of a real catalog's size and shape.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""usage: make-sample-playlist.py <output.m3u> [channels] [--plain]

The PC renderer and the tests browse this instead of a real catalog. The
channels are invented: no name here is a real broadcaster's. The records are
written the way public playlists write them (a size in round brackets, notes
in square ones, a category, alternate addresses for the same channel).
--plain leaves the country and the language out, as the built-in public
catalog does.
"""
import random
import sys

FIRST = ["Aurora", "Harbor", "Cedar", "Meridian", "Lantern", "Summit", "Tidewater", "Juniper",
         "Northlight", "Ember", "Canyon", "Willow", "Granite", "Sable", "Orchard", "Beacon",
         "Copper", "Marigold", "Atlas", "Cobalt", "Saffron", "Linden", "Vesper", "Halcyon",
         "Kestrel", "Onyx", "Prairie", "Quartz", "Riverbend", "Solstice", "Tamarind", "Umber",
         "Verdant", "Wren", "Yarrow", "Zephyr", "Alder", "Bramble", "Clover", "Dune"]
SECOND = ["One", "Two", "24", "Plus", "Central", "World", "Network", "Channel", "TV", "Live",
          "Prime", "Max", "Today", "Now", "Vision", "Zone", "Stream", "Hub"]
CATEGORIES = [("General", 30), ("News", 9), ("Entertainment", 8), ("Music", 8), ("Sports", 5),
              ("Movies", 5), ("Religious", 5), ("Kids", 4), ("Education", 3), ("Documentary", 3),
              ("Lifestyle", 3), ("Culture", 3), ("Series", 2), ("Business", 2), ("Shop", 2),
              ("Travel", 2), ("Cooking", 1), ("Science", 1), ("Weather", 1), ("Outdoor", 1),
              ("Comedy", 1), ("", 1)]
COUNTRIES = [("US", "English"), ("GB", "English"), ("DE", "German"), ("FR", "French"),
             ("ES", "Spanish"), ("IT", "Italian"), ("BR", "Portuguese"), ("MX", "Spanish"),
             ("CA", "English"), ("NL", "Dutch"), ("PL", "Polish"), ("TR", "Turkish"),
             ("JP", "Japanese"), ("KR", "Korean"), ("IN", "Hindi"), ("AU", "English"),
             ("SE", "Swedish"), ("PT", "Portuguese"), ("GR", "Greek"), ("AR", "Spanish"),
             ("EG", "Arabic"), ("ZA", "English"), ("UA", "Ukrainian"), ("CZ", "Czech"),
             ("RO", "Romanian"), ("HU", "Hungarian"), ("FI", "Finnish"), ("NO", "Norwegian")]
SIZES = [("", 22), ("(1080p)", 30), ("(720p)", 26), ("(576p)", 8), ("(480p)", 6), ("(360p)", 3),
         ("(2160p)", 2), ("(540p)", 3)]
NOTES = [("", 80), ("[Not 24/7]", 12), ("[Geo-blocked]", 8)]


def pick(rng, weighted):
    total = sum(weight for _, weight in weighted)
    at = rng.uniform(0, total)
    for value, weight in weighted:
        at -= weight
        if at <= 0:
            return value
    return weighted[-1][0]


def main():
    arguments = [a for a in sys.argv[1:] if not a.startswith("--")]
    plain = "--plain" in sys.argv[1:]
    if not arguments:
        sys.exit(__doc__)
    count = int(arguments[1]) if len(arguments) > 1 else 12886
    rng = random.Random(20261002)
    names = set()
    lines = ["#EXTM3U"]
    while len(names) < count:
        name = f"{rng.choice(FIRST)} {rng.choice(SECOND)}"
        if rng.random() < 0.55:
            name += f" {rng.choice(['East', 'West', 'North', 'South', 'Kids', 'News', 'Sport', 'Gold', 'Extra', 'HD', 'Classic', 'Family', 'Ni\u00f1os', 'T\u00e9l\u00e9', '\u00dcber'])}"
        if name in names:
            name += f" {len(names) % 97 + 2}"
        if name in names:
            continue
        names.add(name)
    # Names in scripts the fonts hold (Cyrillic, Greek) and in one they do not.
    names.update(["Alder \u041a\u0430\u043d\u0430\u043b 5", "Alder \u0395\u03bb\u03bb\u03ac\u03b4\u03b1 2",
                  "Alder \u4e2d\u6587\u9891\u9053"])
    for index, name in enumerate(sorted(names)):
        category = pick(rng, CATEGORIES)
        if name.endswith("Kids"):
            category = "Kids"
        elif name.endswith("News"):
            category = "News"
        elif name.endswith("Sport"):
            category = "Sports"
        country, language = rng.choice(COUNTRIES)
        slug = "".join(c for c in name if c.isascii() and c.isalnum()) or f"ch{index}"
        attributes = [f'tvg-id="{slug}.{country.lower()}"']
        attributes.append(f'tvg-logo="https://logos.example.invalid/{slug.lower()}.png"')
        if not plain:
            attributes.append(f'tvg-country="{country}"')
            attributes.append(f'tvg-language="{language}"')
        attributes.append(f'group-title="{category or "Undefined"}"')
        shown = " ".join(part for part in (name, pick(rng, SIZES), pick(rng, NOTES)) if part)
        lines.append(f'#EXTINF:-1 {" ".join(attributes)},{shown}')
        lines.append(f"https://streams.example.invalid/{slug.lower()}/{index}/index.m3u8")
    with open(arguments[0], "w", encoding="utf-8", newline="\n") as output:
        output.write("\n".join(lines) + "\n")
    print(f"{arguments[0]}: {count} channels{' (plain)' if plain else ''}")


if __name__ == "__main__":
    main()
