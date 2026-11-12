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
}
