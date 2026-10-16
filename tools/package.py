r"""Builds the mod archive "Apocrypha Tentacle Combat <version>.zip": TentacleCombat.esp, the severed meshes, the death
sounds, SKSE\Plugins\TentacleCombat.dll and its ini. The version is the target's set_version in xmake.lua (a trailing
".0" is left out of the name).
usage: python package.py [build dir] [output dir]"""

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build"
OUTPUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "dist"


def version(target: str) -> str:
    text = (ROOT / "xmake.lua").read_text(encoding="utf-8")
    match = re.search(r'target\("' + re.escape(target) + r'"\)\s*\n\s*set_version\("([^"]+)"\)', text)
    full = match.group(1) if match else "0.0.0"
    return re.sub(r"\.0$", "", full)


def main():
    plugin = BUILD / "windows" / "x64" / "releasedbg"
    res = ROOT / "res"
    files = {
        "TentacleCombat.esp": res / "TentacleCombat.esp",
        "SKSE/Plugins/TentacleCombat.dll": plugin / "TentacleCombat.dll",
        "SKSE/Plugins/TentacleCombat.ini": res / "SKSE" / "Plugins" / "TentacleCombat.ini",
    }
    for mesh in sorted((res / "Meshes" / "TentacleCombat").glob("*.nif")):
        files[f"Meshes/TentacleCombat/{mesh.name}"] = mesh
    for sound in sorted((res / "Sound" / "FX" / "TentacleCombat").glob("*.wav")):
        files[f"Sound/FX/TentacleCombat/{sound.name}"] = sound
    missing = [str(p) for p in files.values() if not p.exists()]
    if missing:
        sys.exit("missing: " + ", ".join(missing))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    archive = OUTPUT / f"Apocrypha Tentacle Combat {version('TentacleCombat')}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for entry, path in files.items():
            zf.write(path, entry)
    print(f"wrote {archive} ({archive.stat().st_size / 1024:.0f} KB, {len(files)} files)")


if __name__ == "__main__":
    main()
