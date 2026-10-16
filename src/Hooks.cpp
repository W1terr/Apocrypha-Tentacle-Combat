#include "Hooks.h"

#include "Tentacles.h"

namespace Hooks
{
	namespace
	{
		// once a frame, on the main thread, while the game runs
		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				Tentacles::Update(a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// TESObjectREFR::UpdateAnimation: runs the animation graph of non-actor refs. A tentacle we pose ourselves
		// keeps its graph frozen (in its idle state) until it is back under water.
		struct RefUpdateAnimation
		{
			static void thunk(RE::TESObjectREFR* a_this, float a_delta)
			{
				if (Tentacles::IsControlled(a_this)) {
					return;
				}
				func(a_this, a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		class EventSink :
			public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
			public RE::BSTEventSink<RE::TESHitEvent>,
			public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			static EventSink* GetSingleton()
			{
				static EventSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				if (a_event) {
					Tentacles::OnLoaded(a_event->formID, a_event->loaded);
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (a_event && a_event->target && a_event->cause) {
					Tentacles::OnHit(a_event->target->GetFormID(), a_event->cause->GetFormID());
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (a_event && a_event->tag == "HitFrame"sv) {
					Tentacles::OnMeleeSwing();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = player.write_vfunc(0xAD, PlayerUpdate::thunk);

		REL::Relocation<std::uintptr_t> ref{ RE::VTABLE_TESObjectREFR[0] };
		RefUpdateAnimation::func = ref.write_vfunc(0x7D, RefUpdateAnimation::thunk);

		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton(); events) {
			events->AddEventSink<RE::TESObjectLoadedEvent>(EventSink::GetSingleton());
			events->AddEventSink<RE::TESHitEvent>(EventSink::GetSingleton());
		}
		logs::info("Hooks installed");
	}

	void WatchPlayer()
	{
		auto* const player = RE::PlayerCharacter::GetSingleton();
		if (player && player->AddAnimationGraphEventSink(EventSink::GetSingleton())) {
			logs::info("Watching the player's attacks");
		}
	}
}
