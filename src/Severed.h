#pragma once

// The cut off half of a tentacle: one moveable static from our plugin (SeveredTentacle.nif: a skinned tube on a chain
// of capsule bodies). It is placed straight, starting at the cut and pointing the way the tentacle's outer part
// pointed, and pushed with the motion that part had; physics does the rest.
namespace Severed
{
	// a_heading: the half's +X axis points this way (radians, game heading convention)
	bool Spawn(RE::TESObjectREFR* a_tentacle, const RE::NiPoint3& a_at, float a_heading, const RE::NiPoint3& a_velocity);

	// each frame: halves whose 3D and physics just came up get their push. Returns the tentacles whose half is there
	// now (they show their stump from then on).
	std::vector<RE::FormID> Update(float a_delta);

	void Reset();
}
