#pragma once

// Data/SKSE/Plugins/TentacleCombat.ini (all optional). Distances are in game units for a tentacle at scale 1.0
// and grow with the tentacle's own scale.
namespace Settings
{
	struct Values
	{
		float engageDistance{ 430.0f };   // a tentacle rises when you come this close (and are in front of it)
		float retreatDistance{ 750.0f };  // ... and sinks back when you are farther than this
		float turnLimit{ 180.0f };        // degrees it can turn away from where it was placed, each side (180 = all around)
		float strikePauseMin{ 0.5f };     // seconds it sways between strikes
		float strikePauseMax{ 1.5f };
		float damageMult{ 1.0f };         // x the vanilla leveled damage (100 - 350)
		bool  disease{ true };            // vanilla trap disease chance (2.5 %)
		bool  verboseLog{ false };
	};

	void          Load();
	const Values& Get();
}
