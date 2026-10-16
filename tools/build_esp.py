"""Writes TentacleCombat.esp (ESL flagged, master Skyrim.esm only, no edits to vanilla records):

  MSTT  TC_SeveredTentacle        the severed tentacle half (moveable static: havok physics, can't be picked up)
  SNDR  TC_TentacleDeathSD        the death sound (tools/make_sounds.py), positional

The DLL finds everything by local form id (see src/MeshData.h and src/Forms.h).
usage: python build_esp.py [output.esp]
"""
import json
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "res" / "TentacleCombat.esp"
SOUNDS = ROOT / "res" / "Sound" / "FX" / "TentacleCombat"
OWN = 0x01000000  # this plugin (one master)

# Skyrim.esm forms
CATEGORY_SFX = 0x172A1           # AudioCategorySFX (the "Effects" volume slider)
OUTPUT_MONO_3D = 0xD78B4         # SOMMono01400_verb: positional with reverb, 150 - 1400 units
DESCRIPTOR_STANDARD = 0x1EEF540A  # BGSStandardSoundDef

DEATH_SOUND = 0x8F0              # keep in sync with src/Forms.h
SEVERED = 0x800                  # keep in sync with src/MeshData.h (tools/make_severed.py)


def sub(typ, data):
    if isinstance(data, str):
        data = data.encode("cp1252") + b"\0"
    return typ.encode() + struct.pack("<H", len(data)) + data


def u32(v):
    return struct.pack("<I", v)


def record(typ, formid, subs, flags=0):
    body = b"".join(subs)
    return typ.encode() + struct.pack("<IIIIHH", len(body), flags, formid, 0, 44, 0) + body


def group(label, contents):
    body = b"".join(contents)
    return b"GRUP" + struct.pack("<I", 24 + len(body)) + label.encode() + struct.pack("<iHHHH", 0, 0, 0, 0, 0) + body


def sndr(local, edid, files, output, priority=128, attenuation_db=0.0):
    subs = [sub("EDID", edid), sub("CNAM", u32(DESCRIPTOR_STANDARD)), sub("GNAM", u32(CATEGORY_SFX))]
    subs += [sub("ANAM", f) for f in files]
    subs += [
        sub("ONAM", u32(output)),
        sub("LNAM", bytes([0, 0, 0, 0])),
        # frequency shift, frequency variance, priority, db variance, static attenuation (1/100 dB)
        sub("BNAM", struct.pack("<bbBBH", 0, 4, priority, 0, int(attenuation_db * 100))),
    ]
    return record("SNDR", OWN | local, subs)


def mstt(local, edid, model, bounds):
    (x0, y0, z0), (x1, y1, z1) = bounds
    return record("MSTT", OWN | local, [
        sub("EDID", edid),
        sub("OBND", struct.pack("<6h", x0, y0, z0, x1, y1, z1)),
        sub("MODL", model),
        sub("DATA", b"\0"),
    ])


def main():
    bounds = json.loads((ROOT / "tools" / "bounds.json").read_text())
    statics = [mstt(SEVERED, "TC_SeveredTentacle", r"TentacleCombat\SeveredTentacle.nif", bounds["SeveredTentacle.nif"])]
    deaths = sorted(SOUNDS.glob("death_*.wav"))
    if not deaths:
        sys.exit("no death sounds: run make_sounds.py first")
    sounds = [sndr(DEATH_SOUND, "TC_TentacleDeathSD", ["Data\\Sound\\FX\\TentacleCombat\\" + d.name for d in deaths],
                   OUTPUT_MONO_3D, priority=200)]
    body = group("MSTT", statics) + group("SNDR", sounds)
    count = len(statics) + len(sounds) + 2
    header = record("TES4", 0, [
        sub("HEDR", struct.pack("<f", 1.71) + struct.pack("<i", count) + u32(0x900)),
        sub("CNAM", "W1ter"),
        sub("SNAM", "Apocrypha Tentacle Combat"),
        sub("MAST", "Skyrim.esm"), sub("DATA", b"\0" * 8),
    ], flags=0x200)  # ESL
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(header + body)
    print(f"wrote {OUT} ({len(statics)} severed half, {len(sounds)} sound descriptor)")


if __name__ == "__main__":
    main()
