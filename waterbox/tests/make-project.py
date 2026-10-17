#!/usr/bin/env python3
"""Writes a .chimeraProject for run-frontend.sh, by hand, as the wizard would.

A Sonic Mania project is its settings, its firmware pin (the game's data
file, by SHA-1) and an input log in the panel's own button names. The movie, if
given, is a frame a line, player 1's buttons held as letters (U D L R, A B
C, X Y Z, S Start, s Select; # comments), turned into Chimera's log rows.

usage: make-project.py <package> <out.chimeraProject> <frames> --data <Data.rsdk>
                       [--movie <movie>] [--settings <json>] [--log-out <path>]
--log-out also writes the rows alone, as chimera-run takes a movie.
"""
import argparse
import hashlib
import json
import zipfile

LETTERS = {"Up": "U", "Down": "D", "Left": "L", "Right": "R", "A": "A", "B": "B", "C": "C",
           "X": "X", "Y": "Y", "Z": "Z", "Start": "S", "Select": "s"}


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("package")
    ap.add_argument("out")
    ap.add_argument("frames", type=int)
    ap.add_argument("--data", required=True)
    ap.add_argument("--movie")
    ap.add_argument("--settings", default="{}")
    ap.add_argument("--log-out")
    a = ap.parse_args()

    cfg = json.loads(zipfile.ZipFile(a.package).read("waterbox.config"))
    settings = {d["name"]: d["default"] for d in cfg["settings"]}
    settings.update(json.loads(a.settings))
    buttons = cfg["input"]["buttons"]
    groups = [[b for b in buttons if b.startswith("P1 ")], [b for b in buttons if b.startswith("P2 ")]]
    rows = []
    if a.movie:
        for line in open(a.movie):
            if line.startswith("#"):
                continue
            held = line.strip()
            rows.append(["".join(LETTERS[b[3:]] if b in groups[0] and LETTERS[b[3:]] in held else "." for b in g) for g in groups])
    while len(rows) < a.frames:
        rows.append(["." * len(g) for g in groups])
    rows = ["|" + "|".join(r) + "|" for r in rows[:a.frames]]
    if a.log_out:
        open(a.log_out, "w").write("\n".join(rows) + "\n")
    log = "[Input]\nLogKey:" + "".join("#" + "".join(b + "|" for b in g) for g in groups) + "\n" + "\n".join(rows) + "\n[/Input]\n"

    project = {
        "id": "rsdkv5-frontend-gate",
        "title": "Sonic Mania through Chimera",
        "description": "written by waterbox/tests/run-frontend.sh",
        "core": {"name": cfg["coreName"], "version": cfg["version"], "sha1": sha1(a.package)},
        "rerecords": 0,
        "files": [],
        "settings": settings,
        "firmware": [{"id": d["id"], "sha1": sha1(a.data)} for d in cfg["firmware"]],
        "coreCache": [],
        "input": log,
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Project File v1.1",
            "Platform": cfg["systemId"],
            "SHA1": "",
            "LastInputFrame": str(a.frames - 1),
            "VsyncNumerator": str(cfg["video"]["vsyncNumerator"]),
            "VsyncDenominator": str(cfg["video"]["vsyncDenominator"]),
        },
    }
    with open(a.out, "w") as f:
        json.dump(project, f, indent="\t")


if __name__ == "__main__":
    main()
