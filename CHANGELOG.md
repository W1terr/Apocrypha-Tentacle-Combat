# Changelog

## 1.2

- One strike takes at most 35% of your max health (`fMaxHitPercent` in the INI, 0 = vanilla damage as it is). The
  cap counts what really comes through after armor, blocking, difficulty and combat overhauls, so a high level
  character on Legendary is no longer nearly one-shot.

## 1.1.1

First release.

- Apocrypha's tentacle traps rise when you come near, stay up and keep fighting. They sway, turn all the way around to
  follow you, lead you when you move and strike again and again with the vanilla attack. They sink back only when
  you're far away. Where several tentacles share one spot, the one facing you rises.
- One hit with anything cuts a tentacle in half: any melee weapon or fists, arrows, bolts and spell projectiles. The
  outer half flies off and lands as a physics object that stays where it falls. The stump writhes and sinks with a cut
  face.
- A killed tentacle never comes back, also after leaving the area or loading a save.
- New death sound, a black ink burst and black blood on the ground.
- Settings in `SKSE/Plugins/TentacleCombat.ini`: rise and sink distances, turn limit, pause between strikes, damage
  (vanilla by default) and the vanilla trap disease chance.
