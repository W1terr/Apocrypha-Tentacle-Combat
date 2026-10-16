#include "Forms.h"

#include "MeshData.h"

namespace Forms
{
	namespace
	{
		All forms;

		template <class T>
		T* Find(std::uint32_t a_id, const char* a_file)
		{
			auto* const data = RE::TESDataHandler::GetSingleton();
			auto* const form = data ? data->LookupForm(a_id, a_file) : nullptr;
			auto* const typed = form ? form->As<T>() : nullptr;
			if (!typed) {
				logs::warn("Form {:X} in {} not found", a_id, a_file);
			}
			return typed;
		}
	}

	bool Load()
	{
		auto* const tentacle = Find<RE::TESObjectACTI>(0x02BBC2, "Dragonborn.esm");
		forms.tentacle = tentacle;
		forms.windup = Find<RE::BGSSoundDescriptorForm>(0x03A478, "Dragonborn.esm");
		forms.hit = Find<RE::BGSSoundDescriptorForm>(0x03A477, "Dragonborn.esm");
		forms.inkBurst = Find<RE::BGSExplosion>(0x039E48, "Dragonborn.esm");
		forms.inkImpact = Find<RE::BGSImpactDataSet>(0x03CEF7, "Dragonborn.esm");
		for (const std::uint32_t id : { 0x10A24A, 0x10A24C, 0x10A24D, 0x10A24E, 0x10A24F, 0x10A250 }) {
			if (auto* spell = Find<RE::SpellItem>(id, "Skyrim.esm"); spell) {
				forms.diseases.push_back(spell);
			}
		}
		forms.death = Find<RE::BGSSoundDescriptorForm>(kDeathSound, kPlugin);
		if (auto* form = RE::TESDataHandler::GetSingleton()->LookupForm(MeshData::kSeveredForm, kPlugin); form && form->IsBoundObject()) {
			forms.severed = static_cast<RE::TESBoundObject*>(form);
		}
		logs::info("Forms: tentacle {}, severed half {}, sounds {}/{}/{}, ink {}/{}", tentacle != nullptr, forms.severed != nullptr,
			forms.windup != nullptr, forms.hit != nullptr, forms.death != nullptr,
			forms.inkBurst != nullptr, forms.inkImpact != nullptr);
		if (!tentacle) {
			logs::error("Dragonborn's tentacle trap was not found: the mod stays off");
		}
		return tentacle != nullptr;
	}

	const All& Get() { return forms; }
}
