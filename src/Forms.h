#pragma once

// Game forms the mod uses (looked up once after the data loaded).
namespace Forms
{
	inline constexpr auto          kPlugin = "TentacleCombat.esp";
	inline constexpr std::uint32_t kDeathSound = 0x8F0;  // keep in sync with tools/build_esp.py

	struct All
	{
		RE::TESBoundObject*          tentacle{ nullptr };     // Dragonborn DLC2TrapApoTentacle (the ones in the book dungeons)
		RE::BGSSoundDescriptorForm*  windup{ nullptr };       // TRPApocryphaTentacleWindup
		RE::BGSSoundDescriptorForm*  hit{ nullptr };          // TRPApocryphaTentacleHit
		RE::BGSSoundDescriptorForm*  death{ nullptr };        // ours
		RE::BGSExplosion*            inkBurst{ nullptr };     // DLC2MkTentacleExplosion (Miraak's staff tentacles)
		RE::BGSImpactDataSet*        inkImpact{ nullptr };    // DLC2LurkerGooImpactSet (black goo, black blood decals)
		std::vector<RE::SpellItem*>  diseases;                // the vanilla trap diseases
		RE::TESBoundObject*          severed{ nullptr };      // ours: SeveredTentacle.nif
	};

	bool       Load();
	const All& Get();
}
