#!/usr/bin/env python3
"""make-mods.py - the mods tests' own content, written when the tests run.

No game can be written for this core (Sonic Mania's logic is compiled in),
and no mod is downloaded. What the mods tests need is a data file whose
files a mod can replace, and mods: so this writes

  Data.rsdk    a Retro Engine v5 data file of one file, Data/Game/GameConfig.bin:
               the game's title CHIMERA PACK, version PACK, one stage list.
               The engine runs it with nothing to play, and its developer
               menu (the Developer Menu setting) draws the title and the
               version, so what a mod changes there is in the picture.
and the mods, each a zip as a user would give it:
  alpha.zip    the folder Alpha: mod.ini, and a GameConfig.bin of its own
               (title ALPHA MOD); its ConfigFile makes the engine's mod
               loader write modConfig.cfg into its folder when it loads
  beta.zip     mod.ini at the zip's root (the mod is named beta, as the zip),
               title BETA MOD, stored without compression
  pair.zip     two folders, One (ONE MOD) and Two (TWO MOD), and the
               modconfig.ini upstream keeps beside them: Two first
  pairbyname.zip  the same folders without the modconfig.ini: by name, One
               first
  xml.zip      the folder Xml, with no GameConfig.bin but a Game.xml, which
               the mod loader reads on top of the data file's: its title
               XML MOD
  native.zip   the folder Native, whose mod.ini names a LogicFile (a library
               of compiled code)
  wrong.zip    the folder Old, for version 4 of the engine (TargetVersion=4)
  nomod.zip    a readme and no mod.ini
  notzip.zip   text, not a zip

usage: make-mods.py <folder>
"""
import hashlib
import io
import os
import struct
import sys
import zipfile


def text(s):
    b = s.encode()
    return bytes([len(b)]) + b


def game_config(title, version):
    """Data/Game/GameConfig.bin as RetroEngine.cpp LoadGameConfig reads it
    (RSDKv5U, revision 3): no objects, no palette rows, no sound effects,
    one stage list of one stage, no global variables"""
    out = b"CFG\0" + text(title) + text("Chimera mods test") + text(version)
    out += bytes([0]) + struct.pack("<H", 0) + bytes([0])   # category, stage, objects
    out += struct.pack("<H", 0) * 8                          # palette banks: no rows
    out += bytes([0])                                         # sound effects
    out += struct.pack("<H", 1) + bytes([1])                  # stages, lists
    out += text("TESTS") + bytes([1])
    out += text(title) + text("Test") + text("1") + bytes([0xFF])
    out += bytes([0])                                         # global variables
    return out


def data_pack(files):
    """a Retro Engine v5 data file (Reader.cpp LoadDataPack): RSDKv5, the
    count, each file's MD5 of its lower-case name (each 4 bytes reversed, as
    the engine reads them), offset and size, then the files, not encrypted"""
    head = b"RSDKv5" + struct.pack("<H", len(files))
    at = len(head) + 24 * len(files)
    table, body = b"", b""
    for name, data in files:
        d = hashlib.md5(name.lower().encode()).digest()
        table += b"".join(d[i:i + 4][::-1] for i in range(0, 16, 4))
        table += struct.pack("<II", at + len(body), len(data))
        body += data
    return head + table + body


def mod_ini(name, extra=""):
    return ("Name=%s\nDescription=a test mod of the Chimera core\nAuthor=Chimera\nVersion=1.0.0\n%s" % (name, extra)).encode()


def write_zip(path, entries, method=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, "w", method) as z:
        for name, data in entries:
            info = zipfile.ZipInfo(name, (2026, 11, 15, 0, 0, 0))
            info.compress_type = method
            z.writestr(info, data)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    cfg = "Data/Game/GameConfig.bin"
    with open(os.path.join(out, "Data.rsdk"), "wb") as f:
        f.write(data_pack([(cfg, game_config("CHIMERA PACK", "PACK"))]))
    write_zip(os.path.join(out, "alpha.zip"), [
        ("Alpha/mod.ini", mod_ini("Alpha Mod", "ConfigFile=config\n")),
        ("Alpha/config.ini", b"[Look]\nShade=7\n"),
        ("Alpha/" + cfg, game_config("ALPHA MOD", "ALPHA")),
    ])
    write_zip(os.path.join(out, "beta.zip"), [
        ("mod.ini", mod_ini("Beta Mod")),
        (cfg, game_config("BETA MOD", "BETA")),
    ], zipfile.ZIP_STORED)
    write_zip(os.path.join(out, "pair.zip"), [
        ("mods/modconfig.ini", b"[Mods]\nTwo=y\nOne=y\n"),
        ("mods/One/mod.ini", mod_ini("One Mod")),
        ("mods/One/" + cfg, game_config("ONE MOD", "ONE")),
        ("mods/Two/mod.ini", mod_ini("Two Mod")),
        ("mods/Two/" + cfg, game_config("TWO MOD", "TWO")),
    ])
    write_zip(os.path.join(out, "pairbyname.zip"), [
        ("mods/One/mod.ini", mod_ini("One Mod")),
        ("mods/One/" + cfg, game_config("ONE MOD", "ONE")),
        ("mods/Two/mod.ini", mod_ini("Two Mod")),
        ("mods/Two/" + cfg, game_config("TWO MOD", "TWO")),
    ])
    write_zip(os.path.join(out, "xml.zip"), [
        ("Xml/mod.ini", mod_ini("Xml Mod")),
        ("Xml/Data/Game/Game.xml", b'<?xml version="1.0"?>\n<game>\n\t<title name="XML MOD"/>\n</game>\n'),
    ])
    write_zip(os.path.join(out, "native.zip"), [
        ("Native/mod.ini", mod_ini("Native Mod", "LogicFile=NativeMod.dll\n")),
        ("Native/NativeMod.dll", b"MZ" + bytes(62)),
        ("Native/" + cfg, game_config("NATIVE MOD", "NATIVE")),
    ])
    write_zip(os.path.join(out, "wrong.zip"), [
        ("Old/mod.ini", mod_ini("Old Mod", "TargetVersion=4\n")),
    ])
    write_zip(os.path.join(out, "nomod.zip"), [("readme.txt", b"no mod here\n")])
    with open(os.path.join(out, "notzip.zip"), "wb") as f:
        f.write(b"this is not a zip\n")


if __name__ == "__main__":
    main()
