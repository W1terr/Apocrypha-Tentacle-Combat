#include "Tentacles.h"

#include "Anim.h"
#include "Forms.h"
#include "MathUtil.h"
#include "MeshData.h"
#include "Papyrus.h"
#include "Settings.h"
#include "Severed.h"

namespace Tentacles
{
	namespace
	{
		using M::kDegrees;
		using RE::NiPoint3;

		enum class State
		{
			kIdle,    // under water, the vanilla graph runs (and shows the same pose)
			kRise,    // clip 0 -> 0.65
			kSettle,  // clip 1.95 -> 2.10 (where the rise ends, and the strike ends)
			kSway,    // clip swings 2.10 <-> 2.45: upright, swaying, watching you
			kStrike,  // clip 0.72 -> 2.10
			kSink,    // clip runs on to 4.0 (back under water)
			kDying,   // cut: the stump writhes and sinks, then the ref is disabled for good
		};

		constexpr std::array<const char*, Anim::kBones> kBoneNames{ "Base01", "Bone01", "Bone02", "Bone03", "Bone04", "Bone05",
			"Bone06", "Bone07", "Bone08", "Bone09", "Bone10" };
		constexpr float kTipLength = 34.0f;          // Bone10's tip, along its x axis
		constexpr float kSwaySpeed = 2.6f;           // radians per second of the sway's phase
		constexpr float kRiseFade = 0.15f;
		constexpr float kStrikeFade = 0.25f;
		constexpr float kStrikeAim = 15.0f * kDegrees;   // it strikes when it faces you this well
		constexpr float kReachMin = 60.0f;               // where its strike lands (unscaled, from its base); closer
		                                                 // than this you are under it and your direction flips fast
		constexpr float kSlamStart = 1.05f;              // clip times of the slam itself: the aim is locked
		constexpr float kSlamEnd = 1.40f;
		constexpr float kImpact = 1.25f;                 // about when it lands
		constexpr float kReachMax = 330.0f;
		constexpr float kPlayerRadius = 22.0f;
		constexpr float kHitStop = 0.09f;                // the cut tentacle freezes this long (and until its half is there)
		constexpr float kHalfWait = 0.6f;                // ... at most
		constexpr float kRestAfterSink = 1.0f;           // seconds before a sunk tentacle can rise again

		struct Tentacle
		{
			RE::ObjectRefHandle                        handle;
			RE::FormID                                 id{ 0 };
			RE::NiPointer<RE::NiAVObject>              root;
			RE::NiAVObject*                            yawNode{ nullptr };
			std::array<RE::NiAVObject*, Anim::kBones>  bones{};
			RE::NiMatrix3                              yawRest;
			std::vector<std::pair<RE::NiPointer<RE::NiTimeController>, std::uint16_t>> controllers;
			bool                                       controlled{ false };
			float                                      scale{ 1.0f };
			NiPoint3                                   base;  // where it comes out of the water

			State      state{ State::kIdle };
			float      clip{ 0.0f };
			float      phase{ 0.0f };
			float      pause{ 0.0f };
			float      rest{ 0.0f };
			float      yaw{ 0.0f };
			bool       struck{ false };
			float      lost{ 0.0f };  // seconds you have been out of its reach while it's up
			Anim::Pose current{};
			Anim::Pose fadeFrom{};
			float      fade{ 1.0f };
			float      fadeLength{ 0.1f };

			// joint positions in the world: [1] - [10] Bone01 - Bone10, [11] the tip
			std::array<NiPoint3, 12> joints{};
			std::array<NiPoint3, 12> lastJoints{};
			bool                     haveJoints{ false };

			// dying
			int   cutBone{ 0 };
			float freeze{ 0.0f };
			float waited{ 0.0f };
			bool  collapsed{ false };
			bool  sinking{ false };
			float dying{ 0.0f };
		};

		struct Player
		{
			RE::PlayerCharacter* actor{ nullptr };
			NiPoint3             feet;
			NiPoint3             velocity;  // units per second (where you'll be when the strike lands)
			NiPoint3             head;
			float                height{ 128.0f };
			float                heading{ 0.0f };
			bool                 alive{ false };
		};

		std::unordered_map<RE::FormID, Tentacle> tracked;
		std::unordered_set<RE::FormID>          dead;

		// refs whose animation graph is ours right now (read by the animation update hook, any thread)
		std::array<std::atomic<const RE::TESObjectREFR*>, 64> controlled{};

		std::mutex                                   eventLock;
		std::vector<std::pair<RE::FormID, bool>>     loadedEvents;
		std::vector<RE::FormID>                      hitEvents;
		std::atomic<bool>                            meleePending{ false };

		std::unordered_map<std::uint32_t, NiPoint3> projectiles;  // last position of the player's projectiles
		RE::ATTACK_STATE_ENUM                       lastAttackState{ RE::ATTACK_STATE_ENUM::kNone };
		float                                       sinceMelee{ 1.0f };
		float                                       scanTimer{ 0.0f };
		float                                       lastDelta{ 1.0f / 60.0f };

		bool Verbose() { return Settings::Get().verboseLog; }

		const char* Name(State a_state)
		{
			switch (a_state) {
			case State::kIdle:
				return "idle";
			case State::kRise:
				return "rise";
			case State::kSettle:
				return "settle";
			case State::kSway:
				return "sway";
			case State::kStrike:
				return "strike";
			case State::kSink:
				return "sink";
			case State::kDying:
				return "dying";
			}
			return "?";
		}

		void SetControlled(const RE::TESObjectREFR* a_ref, bool a_on)
		{
			if (a_on) {
				for (auto& slot : controlled) {
					if (slot.load() == a_ref) {
						return;
					}
				}
				for (auto& slot : controlled) {
					const RE::TESObjectREFR* empty = nullptr;
					if (slot.compare_exchange_strong(empty, a_ref)) {
						return;
					}
				}
				logs::warn("More than {} tentacles at once", controlled.size());
			} else {
				for (auto& slot : controlled) {
					const RE::TESObjectREFR* mine = a_ref;
					slot.compare_exchange_strong(mine, nullptr);
				}
			}
		}

		void PlayAt(RE::BGSSoundDescriptorForm* a_form, const NiPoint3& a_position)
		{
			auto* const audio = RE::BSAudioManager::GetSingleton();
			RE::BSSoundHandle handle;
			if (!audio || !a_form || !audio->GetSoundHandle(handle, a_form)) {
				return;
			}
			handle.SetPosition(a_position);
			handle.Play();
		}

		// ---------------------------------------------------------------------------------------------- nodes

		bool Bind(Tentacle& a_t, RE::NiAVObject* a_root)
		{
			a_t.root.reset(a_root);
			a_t.yawNode = a_root->GetObjectByName("ApoTentacleTrap01");
			for (int i = 0; i < Anim::kBones; ++i) {
				a_t.bones[i] = a_root->GetObjectByName(kBoneNames[i]);
				if (!a_t.bones[i]) {
					logs::warn("Tentacle {:X}: no {} in its 3D (another mesh?)", a_t.id, kBoneNames[i]);
					return false;
				}
			}
			if (!a_t.yawNode) {
				logs::warn("Tentacle {:X}: no ApoTentacleTrap01 node in its 3D", a_t.id);
				return false;
			}
			a_t.yawRest = a_t.yawNode->local.rotate;

			// the vanilla trigger volume would start the vanilla script (one strike, then waiting): taken out of the
			// physics world for as long as this 3D lives, so only we decide when it rises
			int phantoms = 0;
			RE::BSVisit::TraverseScenegraphCollision(a_root, [&](RE::bhkNiCollisionObject* a_collision) {
				auto* const body = a_collision->body.get();
				if (body && !body->AsBhkRigidBody() && body->GetWorld1()) {
					body->RemoveFromCurrentWorld();
					++phantoms;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (Verbose()) {
				logs::info("Tentacle {:X} bound (scale {:.2f}, {} trigger volume(s) switched off)", a_t.id, a_t.scale, phantoms);
			}
			return true;
		}

		void TakeControl(Tentacle& a_t, RE::TESObjectREFR* a_ref)
		{
			if (a_t.controlled) {
				return;
			}
			a_t.controlled = true;
			SetControlled(a_ref, true);
			// the bones carry old keyframe controllers from the 3ds Max export; keep them from ever posing a bone
			a_t.controllers.clear();
			for (auto* bone : a_t.bones) {
				for (auto* c = bone->GetControllers(); c; c = c->next.get()) {
					a_t.controllers.emplace_back(RE::NiPointer<RE::NiTimeController>(c), c->flags.underlying());
					c->flags.reset(RE::NiTimeController::Flag::kActive);
				}
			}
		}

		void Release(Tentacle& a_t, RE::TESObjectREFR* a_ref)
		{
			if (!a_t.controlled) {
				return;
			}
			a_t.controlled = false;
			SetControlled(a_ref, false);
			for (auto& [controller, flags] : a_t.controllers) {
				controller->flags = static_cast<RE::NiTimeController::Flag>(flags);
			}
			a_t.controllers.clear();
			if (a_t.yawNode) {
				a_t.yawNode->local.rotate = a_t.yawRest;
				RE::NiUpdateData context{};
				a_t.yawNode->Update(context);
			}
		}

		void Apply(Tentacle& a_t, const Anim::Pose& a_pose)
		{
			a_t.yawNode->local.rotate = a_t.yawRest * M::RotZ(a_t.yaw);
			for (int i = 0; i < Anim::kBones; ++i) {
				auto* const bone = a_t.bones[i];
				bone->local.rotate = M::ToMatrix(a_pose.bones[i].q);
				bone->local.translate = a_pose.bones[i].t;
				bone->local.scale = 1.0f;
			}
			if (a_t.collapsed && a_t.cutBone == MeshData::kCutBone) {
				// the outer half is gone: its bones shrink into the stump's last ring = a flat cut face
				auto* const bone = a_t.bones[a_t.cutBone];
				bone->local.translate = { MeshData::kFace[0], MeshData::kFace[1], MeshData::kFace[2] };
				bone->local.scale = 0.001f;
			}
			RE::NiUpdateData context{};
			a_t.yawNode->Update(context);

			a_t.lastJoints = a_t.joints;
			for (int b = 1; b <= 10; ++b) {
				a_t.joints[b] = a_t.bones[b]->world.translate;
			}
			a_t.joints[11] = a_t.bones[10]->world * NiPoint3(kTipLength, 0.0f, 0.0f);
			if (!a_t.haveJoints) {
				a_t.lastJoints = a_t.joints;
				a_t.haveJoints = true;
			}
		}

		void StartFade(Tentacle& a_t, float a_length)
		{
			a_t.fadeFrom = a_t.current;
			a_t.fade = 0.0f;
			a_t.fadeLength = a_length;
		}

		// ---------------------------------------------------------------------------------------------- geometry

		// a world point in the tentacle's own space (unscaled: the vanilla tentacle at scale 1)
		NiPoint3 Local(const Tentacle& a_t, const NiPoint3& a_world)
		{
			const auto& w = a_t.root->world;
			const float scale = w.scale != 0.0f ? 1.0f / w.scale : 1.0f;
			return (M::Transposed(w.rotate) * (a_world - w.translate)) * scale;
		}

		float Radius(const Tentacle& a_t, int a_bone)
		{
			return MeshData::kRadius[std::clamp(a_bone, 1, 10) - 1] * a_t.scale;
		}

		// the last bone that is still there (the stump ends before the cut)
		int LastBone(const Tentacle& a_t) { return a_t.collapsed ? a_t.cutBone - 1 : 10; }

		NiPoint3 Velocity(const Tentacle& a_t, int a_joint)
		{
			return (a_t.joints[a_joint] - a_t.lastJoints[a_joint]) * (1.0f / std::max(lastDelta, 0.005f));
		}

		bool Visible(const Tentacle& a_t)
		{
			switch (a_t.state) {
			case State::kRise:
				return a_t.clip > 0.2f;
			case State::kSettle:
			case State::kSway:
			case State::kStrike:
				return true;
			case State::kSink:
				return a_t.clip < 3.55f;
			default:
				return false;
			}
		}

		// ---------------------------------------------------------------------------------------------- hurting you

		float Damage(const Player& a_player)
		{
			// the vanilla script's leveled damage (DLC2TrapApoTentacle properties, by encounter level)
			constexpr std::array<int, 5>   thresholds{ 30, 38, 46, 54, 62 };
			constexpr std::array<float, 6> damage{ 100.0f, 150.0f, 200.0f, 250.0f, 300.0f, 350.0f };
			const int                      level = a_player.actor->GetLevel();
			std::size_t                    step = 0;
			while (step < thresholds.size() && level > thresholds[step]) {
				++step;
			}
			return damage[step] * Settings::Get().damageMult;
		}

		void CheckStrike(Tentacle& a_t, RE::TESObjectREFR* a_ref, const Player& a_player)
		{
			if (a_t.struck || !a_player.alive) {
				return;
			}
			const NiPoint3 bottom = a_player.feet + NiPoint3(0.0f, 0.0f, 15.0f);
			const NiPoint3 top = a_player.head;
			for (int b = 2; b <= 10; ++b) {
				const auto hit = M::SegmentSegment(a_t.joints[b], a_t.joints[b + 1], bottom, top);
				if (hit.distance > Radius(a_t, b) + kPlayerRadius) {
					continue;
				}
				a_t.struck = true;
				const NiPoint3 at = M::Lerp(a_t.joints[b], a_t.joints[b + 1], hit.s);
				const NiPoint3 velocity = Velocity(a_t, b + 1);
				const float    damage = Damage(a_player);
				Papyrus::CallMethod(static_cast<RE::TESObjectREFR*>(a_player.actor), "ProcessTrapHit", a_ref, damage, 0.0f,
					velocity.x, velocity.y, velocity.z, at.x, at.y, at.z, static_cast<std::int32_t>(0), 0.0f);
				PlayAt(Forms::Get().hit, at);
				RE::ShakeCamera(0.7f, a_player.feet, 0.5f);
				const auto& diseases = Forms::Get().diseases;
				if (Settings::Get().disease && !diseases.empty() && M::Random(0.0f, 100.0f) <= 2.5f) {
					a_player.actor->AddSpell(diseases[static_cast<std::size_t>(M::Random(0.0f, static_cast<float>(diseases.size()) - 0.01f))]);
				}
				if (Verbose()) {
					logs::info("Tentacle {:X} hit you for {:.0f} (bone {})", a_t.id, damage, b);
				}
				return;
			}
		}

		// ---------------------------------------------------------------------------------------------- being cut

		void Kill(Tentacle& a_t, RE::TESObjectREFR* a_ref, const NiPoint3& a_direction, const char* a_how)
		{
			if (a_t.state == State::kIdle || a_t.state == State::kDying || !a_t.haveJoints) {
				return;
			}
			// always cut at the start of Bone06: the half is one model
			const int cut = MeshData::kCutBone;

			a_t.state = State::kDying;
			a_t.cutBone = cut;
			a_t.freeze = kHitStop;
			a_t.waited = 0.0f;
			a_t.collapsed = false;
			a_t.sinking = false;
			a_t.dying = 0.0f;
			if ((a_t.id >> 24) != 0xFF) {
				dead.insert(a_t.id);
			}

			// the half starts at the cut, pointing the way the outer part pointed (seen from above), and flies off with
			// the outer part's average motion, pushed away from the blow
			NiPoint3 along = a_t.joints[11] - a_t.joints[cut];
			along.z = 0.0f;
			if (M::Length(along) < 20.0f) {
				along = NiPoint3(a_direction.x, a_direction.y, 0.0f);
			}
			along = M::Normalized(along, NiPoint3(1.0f, 0.0f, 0.0f));
			const float heading = std::atan2(-along.y, along.x);  // a ref's +X points to (cos z, -sin z)
			NiPoint3    velocity;
			for (int j = cut; j <= 11; ++j) {
				velocity += Velocity(a_t, j) * (1.0f / static_cast<float>(12 - cut));
			}
			NiPoint3 push = M::Normalized(NiPoint3(a_direction.x, a_direction.y, 0.0f), along) * 150.0f;
			push.z = 80.0f;
			velocity += push;
			if (const float speed = M::Length(velocity); speed > 700.0f) {
				velocity = velocity * (700.0f / speed);
			}
			if (!Severed::Spawn(a_ref, a_t.joints[cut], heading, velocity)) {
				a_t.collapsed = true;
			}

			// its keyframed collision would shove the half around: out of the world (the tentacle is gone soon anyway)
			RE::BSVisit::TraverseScenegraphCollision(a_t.root.get(), [](RE::bhkNiCollisionObject* a_collision) {
				auto* const body = a_collision->body.get();
				if (body && body->GetWorld1()) {
					body->RemoveFromCurrentWorld();
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});

			const auto& forms = Forms::Get();
			const NiPoint3 at = a_t.joints[cut];
			PlayAt(forms.death, at);
			if (auto* data = RE::TESDataHandler::GetSingleton(); data && forms.inkBurst) {
				data->CreateReferenceAtLocation(forms.inkBurst, at, NiPoint3(0.0f, 0.0f, 0.0f), a_ref->GetParentCell(),
					a_ref->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), false, true);
			}
			if (auto* impacts = RE::BGSImpactManager::GetSingleton(); impacts && forms.inkImpact) {
				NiPoint3 down(0.0f, 0.0f, -1.0f);
				impacts->PlayImpactEffect(a_ref, forms.inkImpact, kBoneNames[cut - 1], down, 600.0f, false, false);
			}
			logs::info("Tentacle {:X} cut ({})", a_t.id, a_how);
		}

		void Dying(Tentacle& a_t, RE::TESObjectREFR* a_ref, float a_delta)
		{
			if (!a_t.collapsed) {
				a_t.waited += a_delta;
				if (a_t.waited >= kHalfWait) {
					a_t.collapsed = true;
				}
			}
			if (a_t.freeze > 0.0f || !a_t.collapsed) {
				a_t.freeze -= a_delta;
				Apply(a_t, a_t.current);  // hit stop (the collapse shows as soon as the half is there)
				return;
			}
			if (!a_t.sinking) {
				a_t.sinking = true;
				StartFade(a_t, 0.3f);
				a_t.clip = Anim::kSinkStart;
			}
			a_t.dying += a_delta;
			a_t.clip = std::min(a_t.clip + a_delta * 0.75f, Anim::kEnd);
			Anim::Pose pose = Anim::Sample(a_t.clip);
			if (a_t.fade < 1.0f) {
				a_t.fade += a_delta / a_t.fadeLength;
				pose = Anim::Blend(a_t.fadeFrom, pose, M::Smooth(a_t.fade));
			}
			// writhing, fading out
			const float strength = 0.35f * std::exp(-1.1f * a_t.dying);
			for (int b = 2; b < a_t.cutBone; ++b) {
				const float angle = strength * std::sin(14.0f * a_t.dying + 0.9f * static_cast<float>(b));
				pose.bones[b].q = M::Mul(pose.bones[b].q, M::AboutZ(angle));
			}
			a_t.current = pose;
			Apply(a_t, pose);
			if (a_t.clip >= Anim::kEnd && a_t.dying > 1.2f) {
				logs::info("Tentacle {:X} is gone for good", a_t.id);
				Release(a_t, a_ref);
				a_ref->Disable();
				a_t.state = State::kIdle;
			}
		}

		// ---------------------------------------------------------------------------------------------- your attacks

		float MeleeReach(const Player& a_player)
		{
			float reach = 0.75f;  // fists
			for (const bool left : { false, true }) {
				auto* const object = a_player.actor->GetEquippedObject(left);
				auto* const weapon = object ? object->As<RE::TESObjectWEAP>() : nullptr;
				if (weapon) {
					reach = std::max(reach, weapon->weaponData.reach);
				}
			}
			float combat = 141.0f;
			if (auto* settings = RE::GameSettingCollection::GetSingleton(); settings) {
				if (auto* setting = settings->GetSetting("fCombatDistance"); setting) {
					combat = setting->GetFloat();
				}
			}
			return combat * reach * a_player.actor->GetScale() + 30.0f;
		}

		void MeleeCheck(const Player& a_player)
		{
			const float    reach = MeleeReach(a_player);
			const NiPoint3 chest = a_player.feet + NiPoint3(0.0f, 0.0f, a_player.height * 0.65f);
			const NiPoint3 forward(std::sin(a_player.heading), std::cos(a_player.heading), 0.0f);
			Tentacle*      target = nullptr;
			float          bestDistance = 1e9f;
			for (auto& [id, t] : tracked) {
				if (!Visible(t) || !t.haveJoints) {
					continue;
				}
				for (int b = 3; b <= LastBone(t); ++b) {
					const float    s = M::ClosestOnSegment(chest, t.joints[b], t.joints[b + 1]);
					const NiPoint3 q = M::Lerp(t.joints[b], t.joints[b + 1], s);
					const NiPoint3 to = q - chest;
					const float    distance = M::Length(to) - Radius(t, b);
					if (distance > reach || distance >= bestDistance) {
						continue;
					}
					const float flat = M::Length2D(to);
					if (flat > 35.0f) {
						const float facing = (to.x * forward.x + to.y * forward.y) / flat;
						if (facing < std::cos(60.0f * kDegrees)) {
							continue;
						}
					}
					const float height = q.z - a_player.feet.z;
					if (height < -80.0f || height > a_player.height + 140.0f) {
						continue;
					}
					bestDistance = distance;
					target = &t;
				}
			}
			if (target) {
				if (auto ref = target->handle.get(); ref) {
					Kill(*target, ref.get(), forward, "melee");
				}
			} else if (Verbose()) {
				logs::info("Swing: no tentacle within {:.0f}", reach);
			}
		}

		void ProjectileCheck(const Player& a_player, float a_delta)
		{
			// nothing up: no need to look at projectiles at all
			auto* const manager = RE::Projectile::Manager::GetSingleton();
			const bool  any = std::ranges::any_of(tracked, [](const auto& a_entry) { return Visible(a_entry.second); });
			if (!manager || !any) {
				projectiles.clear();
				return;
			}
			struct Hit
			{
				RE::FormID tentacle;
				NiPoint3   direction;
			};
			std::vector<Hit>                            hits;
			std::unordered_map<std::uint32_t, NiPoint3> seen;
			{
				RE::BSSpinLockGuard guard(manager->projectileLock);
				for (const auto* list : { &manager->unlimited, &manager->limited }) {
					for (const auto& handle : *list) {
						const auto projectile = handle.get();
						if (!projectile) {
							continue;
						}
						auto& data = projectile->GetProjectileRuntimeData();
						if (data.shooter.get().get() != a_player.actor) {
							continue;
						}
						const NiPoint3 now = projectile->GetPosition();
						const auto     key = handle.native_handle();
						seen[key] = now;
						const auto     last = projectiles.find(key);
						const NiPoint3 before = last != projectiles.end() ? last->second : now - data.linearVelocity * a_delta;
						for (auto& [id, t] : tracked) {
							if (!Visible(t) || !t.haveJoints) {
								continue;
							}
							for (int b = 3; b <= LastBone(t); ++b) {
								const auto hit = M::SegmentSegment(t.joints[b], t.joints[b + 1], before, now);
								if (hit.distance < Radius(t, b) + 8.0f) {
									hits.push_back({ id, M::Normalized(now - before) });
									break;
								}
							}
						}
					}
				}
			}
			projectiles = std::move(seen);
			for (const auto& hit : hits) {
				auto it = tracked.find(hit.tentacle);
				if (it == tracked.end()) {
					continue;
				}
				if (auto ref = it->second.handle.get(); ref) {
					Kill(it->second, ref.get(), hit.direction, "projectile");
				}
			}
		}

		// ---------------------------------------------------------------------------------------------- behavior

		void Think(Tentacle& a_t, RE::TESObjectREFR* a_ref, const Player& a_player, float a_delta)
		{
			const auto& settings = Settings::Get();
			const NiPoint3 local = Local(a_t, a_player.feet);
			const float    distance = M::Length2D(local);
			const float    bearing = std::atan2(local.x, -local.y);  // 0 = where its strike goes (-Y)
			// where you will be when the strike lands: it leads a moving target
			float lead = 0.0f;
			if (a_t.state == State::kStrike && a_t.clip < kImpact) {
				lead = (kImpact - a_t.clip) + std::max(0.0f, 1.0f - a_t.fade) * a_t.fadeLength;
			} else if (a_t.state == State::kSway) {
				lead = 0.55f;
			}
			const NiPoint3 ahead = Local(a_t, a_player.feet + a_player.velocity * std::min(lead, 0.7f));
			const float    target = M::Length2D(ahead) > 30.0f ? std::atan2(ahead.x, -ahead.y) : bearing;
			const bool     turnsFully = settings.turnLimit >= 179.0f;
			const float    limit = turnsFully ? M::kPi : settings.turnLimit * kDegrees;
			const bool     alive = a_player.alive;

			// it rises for you in front of it (anywhere around it when it turns fully), and stays up as long as you
			// are not far away: walking around it or stepping back a little doesn't send it down
			const bool engage = alive && distance < settings.engageDistance && (turnsFully || std::abs(bearing) <= limit + 20.0f * kDegrees) &&
			                    local.z > -150.0f && local.z < 350.0f;
			const bool near = alive && distance < settings.retreatDistance && local.z > -400.0f && local.z < 600.0f;
			const bool reach = distance > kReachMin && distance < kReachMax && local.z > -120.0f && local.z < 260.0f;

			if (a_t.state == State::kIdle) {
				a_t.rest -= a_delta;
				if (!engage || a_t.rest > 0.0f) {
					return;
				}
				// vanilla puts up to four in one spot, each facing another way: one of them at a time
				// (the one facing you best)
				for (const auto& [otherID, other] : tracked) {
					if (otherID == a_t.id || !other.yawNode || !other.root || M::Length(other.base - a_t.base) >= 80.0f) {
						continue;
					}
					if (other.state != State::kIdle) {
						return;
					}
					const NiPoint3 there = Local(other, a_player.feet);
					if (std::abs(std::atan2(there.x, -there.y)) < std::abs(bearing) - 0.01f) {
						return;
					}
				}
				TakeControl(a_t, a_ref);
				a_t.state = State::kRise;
				a_t.clip = 0.0f;
				a_t.fade = 1.0f;
				a_t.lost = 0.0f;
				a_t.haveJoints = false;
				a_t.yaw = turnsFully ? bearing : std::clamp(bearing, -limit, limit);  // turned while still under water
				a_t.current = Anim::Sample(0.0f);
				PlayAt(Forms::Get().windup, a_ref->GetPosition());
				if (Verbose()) {
					logs::info("Tentacle {:X} rises ({:.0f} away, {:.0f} deg)", a_t.id, distance * a_t.scale, bearing / kDegrees);
				}
			}

			// keep facing you: quickly while it rears back (it locks on), barely while the slam comes down
			const bool  windup = a_t.state == State::kStrike && a_t.clip < kSlamStart;
			const bool  slam = a_t.state == State::kStrike && a_t.clip >= kSlamStart && a_t.clip < kSlamEnd;
			const float rate = (slam ? 30.0f : windup ? 420.0f : 220.0f) * kDegrees;
			const float aim = turnsFully ? target : std::clamp(target, -limit, limit);
			a_t.yaw = M::WrapAngle(a_t.yaw + std::clamp(M::WrapAngle(aim - a_t.yaw), -rate * a_delta, rate * a_delta));
			a_t.lost = near ? 0.0f : a_t.lost + a_delta;
			const bool leave = a_t.lost > 1.5f;

			const State before = a_t.state;
			switch (a_t.state) {
			case State::kRise:
				a_t.clip += a_delta;
				if (a_t.clip >= Anim::kRiseEnd) {
					StartFade(a_t, kRiseFade);
					a_t.clip = 1.95f;  // the rise ends in (nearly) this pose
					a_t.state = State::kSettle;
				}
				break;
			case State::kSettle:
				a_t.clip += a_delta;
				if (a_t.clip >= Anim::kStrikeEnd) {
					a_t.clip = Anim::kStrikeEnd;
					a_t.state = State::kSway;
					a_t.phase = -0.5f * M::kPi;
					a_t.pause = M::Random(settings.strikePauseMin, settings.strikePauseMax) * 0.5f;
				}
				break;
			case State::kSway:
				if (leave) {
					a_t.state = State::kSink;  // on from the sway: the clip itself bends down into the water
					break;
				}
				a_t.phase += kSwaySpeed * a_delta;
				a_t.clip = Anim::kSwayCenter + Anim::kSwayAmplitude * std::sin(a_t.phase);
				a_t.pause -= a_delta;
				if (a_t.pause <= 0.0f && reach && alive && std::abs(M::WrapAngle(target - a_t.yaw)) < kStrikeAim &&
					a_t.clip < Anim::kSwayCenter - 0.08f) {
					// leaning back already: rear back and strike
					StartFade(a_t, kStrikeFade);
					a_t.clip = Anim::kStrikeStart;
					a_t.struck = false;
					a_t.state = State::kStrike;
					PlayAt(Forms::Get().windup, a_t.joints[6]);
					if (Verbose()) {
						logs::info("Tentacle {:X} strikes: you are {:.0f} away at {:.0f} deg, it faces {:.0f} deg", a_t.id,
							distance * a_t.scale, bearing / kDegrees, a_t.yaw / kDegrees);
					}
				}
				break;
			case State::kStrike:
			{
				const float was = a_t.clip;
				a_t.clip += a_delta * (a_t.clip < 1.0f ? 0.8f : 1.0f);
				if (was < kSlamStart && a_t.clip >= kSlamStart) {
					const float off = M::WrapAngle(bearing - a_t.yaw) / kDegrees;
					if (std::abs(off) > 60.0f || (!reach && distance < kReachMin)) {
						// you got away (or right under it) while it reared back: no blind slam, back to swaying
						StartFade(a_t, 0.3f);
						a_t.clip = Anim::kStrikeEnd;
						a_t.state = State::kSway;
						a_t.phase = -0.5f * M::kPi;
						a_t.pause = 0.2f;
						if (Verbose()) {
							logs::info("Tentacle {:X} holds its strike: you are {:.0f} deg off, {:.0f} away", a_t.id, off, distance * a_t.scale);
						}
						break;
					}
					if (Verbose()) {
						logs::info("Tentacle {:X} slams: you are {:.0f} deg off, {:.0f} away", a_t.id, off, distance * a_t.scale);
					}
				}
				if (a_t.clip >= Anim::kDamageStart && a_t.clip <= Anim::kDamageEnd) {
					CheckStrike(a_t, a_ref, a_player);
				}
				if (a_t.clip >= Anim::kStrikeEnd) {
					a_t.clip = Anim::kStrikeEnd;
					if (leave) {
						a_t.state = State::kSink;
					} else {
						a_t.state = State::kSway;
						a_t.phase = -0.5f * M::kPi;
						a_t.pause = M::Random(settings.strikePauseMin, settings.strikePauseMax);
					}
				}
				break;
			}
			case State::kSink:
				a_t.clip += a_delta;
				if (a_t.clip >= Anim::kEnd) {
					a_t.clip = Anim::kEnd;
					a_t.current = Anim::Sample(Anim::kEnd);
					a_t.yaw = 0.0f;
					Apply(a_t, a_t.current);
					Release(a_t, a_ref);
					a_t.state = State::kIdle;
					a_t.rest = kRestAfterSink;
					if (Verbose()) {
						logs::info("Tentacle {:X} sank back", a_t.id);
					}
					return;
				}
				break;
			default:
				break;
			}
			if (Verbose() && before != a_t.state) {
				logs::info("Tentacle {:X}: {} -> {} (clip {:.2f}, {:.0f} away)", a_t.id, Name(before), Name(a_t.state), a_t.clip, distance * a_t.scale);
			}

			Anim::Pose pose = Anim::Sample(a_t.clip);
			if (a_t.fade < 1.0f) {
				a_t.fade += a_delta / a_t.fadeLength;
				pose = Anim::Blend(a_t.fadeFrom, pose, M::Smooth(a_t.fade));
			}
			a_t.current = pose;
			Apply(a_t, pose);
		}

		// ---------------------------------------------------------------------------------------------- tracking

		bool IsTentacle(RE::TESObjectREFR* a_ref)
		{
			const auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			return base && base == Forms::Get().tentacle;
		}

		void Track(RE::TESObjectREFR* a_ref)
		{
			if (!IsTentacle(a_ref)) {
				return;
			}
			const auto id = a_ref->GetFormID();
			if (dead.contains(id)) {
				if (!a_ref->IsDisabled()) {
					logs::info("Tentacle {:X} was killed before: disabled", id);
					a_ref->Disable();
				}
				return;
			}
			if (tracked.contains(id) || a_ref->IsDisabled() || !a_ref->Get3D()) {
				return;
			}
			Tentacle t;
			t.handle = a_ref->GetHandle();
			t.id = id;
			tracked.emplace(id, std::move(t));
		}

		void Scan(RE::PlayerCharacter* a_player)
		{
			auto* const tes = RE::TES::GetSingleton();
			if (!tes) {
				return;
			}
			tes->ForEachReferenceInRange(a_player, 6000.0f, [](RE::TESObjectREFR* a_ref) {
				if (IsTentacle(a_ref)) {
					Track(a_ref);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		bool InDragonbornCell(RE::PlayerCharacter* a_player)
		{
			const auto* cell = a_player->GetParentCell();
			const auto* file = cell ? cell->GetFile(0) : nullptr;
			if (file && _stricmp(file->fileName, "Dragonborn.esm") == 0) {
				return true;
			}
			const auto* world = a_player->GetWorldspace();
			file = world ? world->GetFile(0) : nullptr;
			return file && _stricmp(file->fileName, "Dragonborn.esm") == 0;
		}
	}

	void Init()
	{
		dead.reserve(128);
	}

	void Reset()
	{
		for (auto& slot : controlled) {
			slot.store(nullptr);
		}
		tracked.clear();
		projectiles.clear();
		Severed::Reset();
		meleePending = false;
		scanTimer = 0.0f;
		std::scoped_lock lock(eventLock);
		loadedEvents.clear();
		hitEvents.clear();
	}

	bool IsControlled(const RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return false;
		}
		for (const auto& slot : controlled) {
			if (slot.load(std::memory_order_relaxed) == a_ref) {
				return true;
			}
		}
		return false;
	}

	void OnLoaded(RE::FormID a_id, bool a_loaded)
	{
		std::scoped_lock lock(eventLock);
		loadedEvents.emplace_back(a_id, a_loaded);
	}

	void OnMeleeSwing() { meleePending = true; }

	void OnHit(RE::FormID a_target, RE::FormID a_cause)
	{
		if (a_cause != 0x14) {
			return;
		}
		std::scoped_lock lock(eventLock);
		hitEvents.push_back(a_target);
	}

	void Update(float a_delta)
	{
		const auto& forms = Forms::Get();
		auto* const actor = RE::PlayerCharacter::GetSingleton();
		if (!forms.tentacle || !actor || a_delta <= 0.0f) {
			return;
		}
		a_delta = std::min(a_delta, 0.1f);
		lastDelta = a_delta;

		std::vector<std::pair<RE::FormID, bool>> loaded;
		std::vector<RE::FormID>                  hitsOnTentacles;
		{
			std::scoped_lock lock(eventLock);
			loaded.swap(loadedEvents);
			hitsOnTentacles.swap(hitEvents);
		}
		for (const auto& [id, isLoaded] : loaded) {
			if (isLoaded) {
				auto* const form = RE::TESForm::LookupByID(id);
				Track(form ? form->AsReference() : nullptr);
			}
		}
		scanTimer -= a_delta;
		if (scanTimer <= 0.0f) {
			scanTimer = 2.0f;
			if (InDragonbornCell(actor) || !tracked.empty()) {
				Scan(actor);
			}
		}
		if (tracked.empty()) {
			Severed::Update(a_delta);
			meleePending = false;
			lastAttackState = RE::ATTACK_STATE_ENUM::kNone;
			return;
		}

		Player player;
		player.actor = actor;
		player.feet = actor->GetPosition();
		static NiPoint3 lastFeet = player.feet;
		const NiPoint3  step = player.feet - lastFeet;
		lastFeet = player.feet;
		// (a teleport or a load isn't a run)
		NiPoint3        now = M::Length(step) < 60.0f ? step * (1.0f / a_delta) : NiPoint3(0.0f, 0.0f, 0.0f);
		now.z = 0.0f;
		static NiPoint3 velocity;  // smoothed: frame to frame steps are noisy
		velocity = velocity + (now - velocity) * std::min(1.0f, a_delta * 8.0f);
		player.velocity = velocity;
		player.height = std::max(actor->GetHeight(), 60.0f);
		player.head = player.feet + NiPoint3(0.0f, 0.0f, player.height - 8.0f);
		player.heading = actor->GetAngleZ();
		player.alive = !actor->IsDead();

		for (auto it = tracked.begin(); it != tracked.end();) {
			Tentacle& t = it->second;
			const auto ref = t.handle.get();
			auto* const root = ref ? ref->Get3D() : nullptr;
			const bool  gone = !ref || ref->IsDeleted() || ref->IsDisabled() || !root ||
			                   (dead.contains(t.id) && t.state != State::kDying);
			if (gone || root != t.root.get()) {
				if (t.controlled) {
					Release(t, ref.get());
				}
				if (gone) {
					if (ref && dead.contains(t.id) && !ref->IsDisabled() && t.state != State::kDying) {
						ref->Disable();
					}
					it = tracked.erase(it);
					continue;
				}
				// (new) 3D
				t.scale = ref->GetScale();
				t.base = ref->GetPosition();
				t.state = State::kIdle;
				t.haveJoints = false;
				if (!Bind(t, root)) {
					t.root.reset(root);
					t.yawNode = nullptr;
				}
			}
			if (!t.yawNode) {
				++it;
				continue;
			}
			if (t.state == State::kDying) {
				Dying(t, ref.get(), a_delta);
			} else {
				Think(t, ref.get(), player, a_delta);
			}
			++it;
		}

		for (const auto ready : Severed::Update(a_delta)) {
			if (auto it = tracked.find(ready); it != tracked.end()) {
				it->second.collapsed = true;
			}
		}

		// your swing lands: the HitFrame animation event, or the attack state turning to "hit" (first person too)
		const auto attack = actor->AsActorState()->GetAttackState();
		bool       swing = meleePending.exchange(false);
		swing |= attack == RE::ATTACK_STATE_ENUM::kHit && lastAttackState != RE::ATTACK_STATE_ENUM::kHit;
		lastAttackState = attack;
		sinceMelee += a_delta;
		if (swing && sinceMelee > 0.15f) {
			sinceMelee = 0.0f;
			MeleeCheck(player);
		}
		ProjectileCheck(player, a_delta);
		// anything else that hit one (by its collision): cut in the middle
		for (const auto id : hitsOnTentacles) {
			auto it = tracked.find(id);
			if (it == tracked.end() || !Visible(it->second)) {
				continue;
			}
			if (auto ref = it->second.handle.get(); ref) {
				Kill(it->second, ref.get(), NiPoint3(0.0f, 0.0f, 0.0f), "hit");
			}
		}
	}

	// -------------------------------------------------------------------------------------------------- co-save

	void Save(SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc->OpenRecord('DEAD', 1)) {
			return;
		}
		const auto count = static_cast<std::uint32_t>(dead.size());
		a_intfc->WriteRecordData(count);
		for (const auto id : dead) {
			a_intfc->WriteRecordData(id);
		}
	}

	void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t)
	{
		if (a_version != 1) {
			return;
		}
		std::uint32_t count = 0;
		if (!a_intfc->ReadRecordData(count)) {
			return;
		}
		for (std::uint32_t i = 0; i < count; ++i) {
			RE::FormID id = 0;
			if (a_intfc->ReadRecordData(id) && a_intfc->ResolveFormID(id, id)) {
				dead.insert(id);
			}
		}
		logs::info("{} killed tentacle(s) in this save", dead.size());
	}

	void Revert()
	{
		dead.clear();
	}
}
