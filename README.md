# Apocrypha Tentacle Combat

SKSE plugin for Skyrim SE/AE (built for 1.7.104, Address Library) that turns Apocrypha's tentacle traps into
something you fight.

- **They fight back.** A tentacle rises when you come near and stays up. It sways, turns to follow you and keeps
  striking while you are in reach. It sinks back only when you leave. The motion is the vanilla tentacle clip itself
  (baked from `play01.hkx`), played and blended by the plugin.
- **One hit kills.** Any melee weapon (fists too), arrows, bolts and spell projectiles cut a tentacle in half (always
  at the start of Bone06).
- **A real half.** The outer half flies off and falls as a physics object: one model (`SeveredTentacle.nif`), a smooth
  tube generated to the vanilla tentacle's shape and texture mapping on a chain of five capsule bodies, with a closed
  cut face. It stays where it lands. The stump writhes and sinks with a flat cut face.
- **Dead stays dead.** A killed tentacle is disabled for good and recorded in the SKSE co-save.
- **Custom death sound.** It is layered from vanilla gore, slime, a Seeker death cry and Miraak's tentacle drop.
  There is also a black ink burst and black blood on the ground.

Only `DLC2TrapApoTentacle` placements are affected (the Black Book realms). Miraak's boss fight tentacles use another
base and stay vanilla. The plugin edits no vanilla records. `TentacleCombat.esp` (ESL flagged) only adds the severed
half and the death sound.

## Files
- `TentacleCombat.esp`: one moveable static (the half) and one sound descriptor
- `Meshes/TentacleCombat/SeveredTentacle.nif`, `Sound/FX/TentacleCombat/death_0N.wav`
- `SKSE/Plugins/TentacleCombat.dll`, `TentacleCombat.ini` (distances, turn limit, strike pause, damage, disease)

## Building
- `tools/bake_anim.py` writes `src/AnimData.h`; `tools/make_severed.py` writes the half and `src/MeshData.h`. It
  needs the nifgen library from the Blender Niftools addon, set by the `NIFGEN` environment variable.
- `tools/make_sounds.py` and `tools/build_esp.py` make the sounds and the plugin.
- `build.bat` builds the DLL with xmake. `tools/package.py <build dir> <output dir>` makes the zip.
