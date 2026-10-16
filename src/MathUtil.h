#pragma once

// Vector, rotation and quaternion helpers on top of the game's NiPoint3 / NiMatrix3.
// NiMatrix3 uses column vectors: parent = R * local, entry[row][column].
namespace M
{
	using RE::NiMatrix3;
	using RE::NiPoint3;

	inline constexpr float kPi = std::numbers::pi_v<float>;
	inline constexpr float kDegrees = kPi / 180.0f;

	inline float Clamp01(float a_value) { return std::clamp(a_value, 0.0f, 1.0f); }

	inline float Smooth(float a_t)
	{
		const float t = Clamp01(a_t);
		return t * t * (3.0f - 2.0f * t);
	}

	inline float Dot(const NiPoint3& a, const NiPoint3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline float Length(const NiPoint3& a) { return std::sqrt(Dot(a, a)); }
	inline float Length2D(const NiPoint3& a) { return std::sqrt(a.x * a.x + a.y * a.y); }
	inline NiPoint3 Lerp(const NiPoint3& a, const NiPoint3& b, float t) { return a + (b - a) * t; }

	inline NiPoint3 Normalized(const NiPoint3& a, const NiPoint3& a_fallback = { 0.0f, 0.0f, 1.0f })
	{
		const float length = Length(a);
		return length > 1e-6f ? a * (1.0f / length) : a_fallback;
	}

	// angle wrapped to [-pi, pi]
	inline float WrapAngle(float a)
	{
		while (a > kPi) {
			a -= 2.0f * kPi;
		}
		while (a < -kPi) {
			a += 2.0f * kPi;
		}
		return a;
	}

	inline NiMatrix3 RotZ(float a_angle)
	{
		const float c = std::cos(a_angle);
		const float s = std::sin(a_angle);
		NiMatrix3   m;
		m.entry[0][0] = c;
		m.entry[0][1] = -s;
		m.entry[0][2] = 0.0f;
		m.entry[1][0] = s;
		m.entry[1][1] = c;
		m.entry[1][2] = 0.0f;
		m.entry[2][0] = 0.0f;
		m.entry[2][1] = 0.0f;
		m.entry[2][2] = 1.0f;
		return m;
	}

	inline NiMatrix3 Transposed(const NiMatrix3& m)
	{
		NiMatrix3 t;
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) {
				t.entry[r][c] = m.entry[c][r];
			}
		}
		return t;
	}

	struct Quat
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
		float w{ 1.0f };

		float Dot(const Quat& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

		Quat Normalized() const
		{
			const float n = std::sqrt(Dot(*this));
			return n > 1e-8f ? Quat{ x / n, y / n, z / n, w / n } : Quat{};
		}
	};

	// normalized lerp along the short way
	inline Quat Nlerp(const Quat& a, Quat b, float t)
	{
		if (a.Dot(b) < 0.0f) {
			b = { -b.x, -b.y, -b.z, -b.w };
		}
		return Quat{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t }.Normalized();
	}

	// a then b in a's frame (Hamilton product a * b)
	inline Quat Mul(const Quat& a, const Quat& b)
	{
		return {
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
			a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
		};
	}

	inline Quat AboutZ(float a_angle) { return { 0.0f, 0.0f, std::sin(a_angle * 0.5f), std::cos(a_angle * 0.5f) }; }

	// closest point on the segment [a, b] to p, as 0 - 1 along it
	inline float ClosestOnSegment(const NiPoint3& p, const NiPoint3& a, const NiPoint3& b)
	{
		const NiPoint3 d = b - a;
		const float    l2 = Dot(d, d);
		return l2 > 1e-6f ? Clamp01(Dot(p - a, d) / l2) : 0.0f;
	}

	inline NiMatrix3 ToMatrix(const Quat& q)
	{
		const float x = q.x, y = q.y, z = q.z, w = q.w;
		NiMatrix3   m;
		m.entry[0][0] = 1.0f - 2.0f * (y * y + z * z);
		m.entry[0][1] = 2.0f * (x * y - z * w);
		m.entry[0][2] = 2.0f * (x * z + y * w);
		m.entry[1][0] = 2.0f * (x * y + z * w);
		m.entry[1][1] = 1.0f - 2.0f * (x * x + z * z);
		m.entry[1][2] = 2.0f * (y * z - x * w);
		m.entry[2][0] = 2.0f * (x * z - y * w);
		m.entry[2][1] = 2.0f * (y * z + x * w);
		m.entry[2][2] = 1.0f - 2.0f * (x * x + y * y);
		return m;
	}

	// closest points of the segments [p0, p1] and [q0, q1]: distance, and where on the first one (0 - 1)
	struct SegmentHit
	{
		float distance;
		float s;
		float t;
	};

	inline SegmentHit SegmentSegment(const NiPoint3& p0, const NiPoint3& p1, const NiPoint3& q0, const NiPoint3& q1)
	{
		const NiPoint3 d1 = p1 - p0;
		const NiPoint3 d2 = q1 - q0;
		const NiPoint3 r = p0 - q0;
		const float    a = Dot(d1, d1);
		const float    e = Dot(d2, d2);
		const float    f = Dot(d2, r);
		float          s = 0.0f;
		float          t = 0.0f;
		if (a <= 1e-6f && e <= 1e-6f) {
			return { Length(r), 0.0f, 0.0f };
		}
		if (a <= 1e-6f) {
			t = Clamp01(f / e);
		} else {
			const float c = Dot(d1, r);
			if (e <= 1e-6f) {
				s = Clamp01(-c / a);
			} else {
				const float b = Dot(d1, d2);
				const float denom = a * e - b * b;
				s = denom > 1e-6f ? Clamp01((b * f - c * e) / denom) : 0.0f;
				t = (b * s + f) / e;
				if (t < 0.0f) {
					t = 0.0f;
					s = Clamp01(-c / a);
				} else if (t > 1.0f) {
					t = 1.0f;
					s = Clamp01((b - c) / a);
				}
			}
		}
		const NiPoint3 c1 = p0 + d1 * s;
		const NiPoint3 c2 = q0 + d2 * t;
		return { Length(c1 - c2), s, t };
	}

	inline float Random(float a_min, float a_max)
	{
		static std::mt19937                   rng{ std::random_device{}() };
		std::uniform_real_distribution<float> dist(a_min, a_max);
		return dist(rng);
	}
}
