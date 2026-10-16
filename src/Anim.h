#pragma once

#include "MathUtil.h"

// The vanilla tentacle clip (AnimData.h, baked from play01.hkx), sampled at any time and blended.
// Clip times worth knowing (seconds): 0 under water, 0 - 0.65 rising, 0.65 - 1.0 rearing back, 1.0 - 1.4 the strike,
// 1.4 - 2.1 coming back up, 2.1 - 2.45 swaying upright, 2.6 - 4.0 sinking back (4.0 = 0).
namespace Anim
{
	inline constexpr int kBones = 11;  // Base01, Bone01 ... Bone10

	struct Bone
	{
		RE::NiPoint3 t;
		M::Quat      q;
	};

	struct Pose
	{
		std::array<Bone, kBones> bones;
	};

	// the clip at a_time (clamped to 0 - 4 s)
	Pose Sample(float a_time);

	// a -> b by a_weight
	Pose Blend(const Pose& a_from, const Pose& a_to, float a_weight);

	// clip times
	inline constexpr float kRiseEnd = 0.65f;
	inline constexpr float kStrikeStart = 0.78f;   // reared back, ready
	inline constexpr float kDamageStart = 0.907f;  // the vanilla script's damage window
	inline constexpr float kDamageEnd = 1.593f;
	inline constexpr float kStrikeEnd = 2.10f;     // back up, leaning back = the sway's back end
	inline constexpr float kSwayCenter = 2.275f;
	inline constexpr float kSwayAmplitude = 0.175f;
	inline constexpr float kSinkStart = 2.62f;
	inline constexpr float kEnd = 4.0f;
}
