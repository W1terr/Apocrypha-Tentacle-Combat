#pragma once

// Apocrypha's tentacle traps (Dragonborn DLC2TrapApoTentacle). Vanilla: the trap script fires when you walk into
// its trigger volume, plays the 4 s clip once (rise, strike, sink) and waits. Here the DLL drives each tentacle near
// you itself with the same clip: it rises, sways and keeps striking at you while you stay close, turns to follow
// you, and sinks back when you leave. One hit with anything cuts it in half; a dead tentacle never comes back.
namespace Tentacles
{
	void Init();
	void Reset();  // a game is loaded or started
	void Update(float a_delta);

	// the animation graph of this ref must not run this frame (any thread)
	bool IsControlled(const RE::TESObjectREFR* a_ref);

	// events (any thread; handled in Update)
	void OnLoaded(RE::FormID a_id, bool a_loaded);
	void OnMeleeSwing();  // the player's attack lands (HitFrame)
	void OnHit(RE::FormID a_target, RE::FormID a_cause);

	// co-save: the dead ones
	void Save(SKSE::SerializationInterface* a_intfc);
	void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
	void Revert();
}
