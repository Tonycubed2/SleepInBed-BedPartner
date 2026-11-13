#pragma once

namespace Patch
{
	enum class State
	{
		kNotInstalled,
		kActive,
		kMissing,
		kWrongVersion,
		kHookFailed,
	};

	// Hooks three functions inside SleepInBedSKSE.dll 1.4. Call once all SKSE plugins are loaded.
	void  Install();
	State GetState();

	// Sends a follower to a bed through Sleep In Bed's own Assign (undress, package, tracking).
	void SendToBed(RE::Actor* a_actor, RE::TESObjectREFR* a_bed);

	// Before a save is loaded (or a new game starts): pending "wait before dressing" timers are dropped.
	void OnLoadingGame();
}
