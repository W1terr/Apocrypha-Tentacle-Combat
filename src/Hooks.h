#pragma once

namespace Hooks
{
	void Install();

	// the player's animation events (melee hits); the player's graphs are rebuilt on load, so this runs again then
	void WatchPlayer();
}
