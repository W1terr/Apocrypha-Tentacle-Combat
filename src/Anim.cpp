#include "Anim.h"

#include "AnimData.h"

namespace Anim
{
	namespace
	{
		Bone Key(int a_frame, int a_bone)
		{
			const auto& k = AnimData::kKeys[a_frame][a_bone];
			return { { k.t[0], k.t[1], k.t[2] }, { k.q[0], k.q[1], k.q[2], k.q[3] } };
		}
	}

	Pose Sample(float a_time)
	{
		const float frame = std::clamp(a_time, 0.0f, AnimData::kDuration) * AnimData::kFPS;
		const int   first = std::min(static_cast<int>(frame), AnimData::kFrames - 1);
		const int   second = std::min(first + 1, AnimData::kFrames - 1);
		const float t = frame - static_cast<float>(first);
		Pose        pose;
		for (int b = 0; b < kBones; ++b) {
			const Bone a = Key(first, b);
			const Bone c = Key(second, b);
			pose.bones[b] = { M::Lerp(a.t, c.t, t), M::Nlerp(a.q, c.q, t) };
		}
		return pose;
	}

	Pose Blend(const Pose& a_from, const Pose& a_to, float a_weight)
	{
		const float w = M::Clamp01(a_weight);
		Pose        pose;
		for (int b = 0; b < kBones; ++b) {
			pose.bones[b] = { M::Lerp(a_from.bones[b].t, a_to.bones[b].t, w), M::Nlerp(a_from.bones[b].q, a_to.bones[b].q, w) };
		}
		return pose;
	}
}
