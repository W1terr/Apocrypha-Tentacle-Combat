#include "Severed.h"

#include "Forms.h"
#include "MathUtil.h"

namespace Severed
{
	namespace
	{
		constexpr float kGiveUp = 2.0f;  // seconds to wait for the half's 3D and physics

		struct Pending
		{
			RE::ObjectRefHandle piece;
			RE::FormID          tentacle;
			RE::NiPoint3        velocity;
			float               age{ 0.0f };
		};

		std::vector<Pending> pending;

		// once its bodies are in the physics world: dynamic, and moving the way the cut off part moved
		bool Push(Pending& a_pending, RE::TESObjectREFR* a_ref)
		{
			auto* const root = a_ref->Get3D();
			if (!root) {
				return false;
			}
			int bodies = 0;
			int inWorld = 0;
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_collision) {
				auto* const body = a_collision->body ? a_collision->body->AsBhkRigidBody() : nullptr;
				if (body) {
					++bodies;
					inWorld += body->GetWorld1() ? 1 : 0;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (bodies == 0 || inWorld < bodies) {
				return false;
			}
			root->SetMotionType(RE::hkpMotion::MotionType::kDynamic, true, true, true);

			const float scale = RE::bhkWorld::GetWorldScale();
			const auto& v = a_pending.velocity;
			int         index = 0;
			RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_collision) {
				auto* const body = a_collision->body ? a_collision->body->AsBhkRigidBody() : nullptr;
				if (body) {
					// the same push for all, a little spin so it doesn't land like a log
					body->SetLinearVelocity(RE::hkVector4(v.x * scale, v.y * scale, v.z * scale, 0.0f));
					const float spin = (index % 2 == 0 ? 1.0f : -1.0f) * M::Random(0.5f, 2.0f);
					body->SetAngularVelocity(RE::hkVector4(0.0f, 0.0f, spin, 0.0f));
					++index;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return true;
		}
	}

	bool Spawn(RE::TESObjectREFR* a_tentacle, const RE::NiPoint3& a_at, float a_heading, const RE::NiPoint3& a_velocity)
	{
		auto* const base = Forms::Get().severed;
		auto* const data = RE::TESDataHandler::GetSingleton();
		if (!base || !data || !a_tentacle) {
			logs::warn("No severed half to place (is TentacleCombat.esp active?)");
			return false;
		}
		const auto handle = data->CreateReferenceAtLocation(base, a_at, RE::NiPoint3(0.0f, 0.0f, a_heading), a_tentacle->GetParentCell(),
			a_tentacle->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), false, true);
		const auto piece = handle.get();
		if (!piece) {
			logs::warn("Couldn't place the severed half");
			return false;
		}
		pending.push_back({ handle, a_tentacle->GetFormID(), a_velocity });
		return true;
	}

	std::vector<RE::FormID> Update(float a_delta)
	{
		std::vector<RE::FormID> ready;
		for (auto it = pending.begin(); it != pending.end();) {
			it->age += a_delta;
			const auto ref = it->piece.get();
			bool       done = false;
			if (!ref) {
				logs::warn("Severed half for {:X} is gone", it->tentacle);
				done = true;
			} else if (Push(*it, ref.get())) {
				done = true;
			} else if (it->age > kGiveUp) {
				logs::warn("Severed half {:X}: no 3D or physics after {:.1f} s", ref->GetFormID(), it->age);
				done = true;
			}
			if (done) {
				ready.push_back(it->tentacle);
				it = pending.erase(it);
			} else {
				++it;
			}
		}
		return ready;
	}

	void Reset() { pending.clear(); }
}
