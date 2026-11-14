#pragma once

namespace Weapons
{
	// "Take weapons off when sleeping dressed": when the player or a follower lies in bed still holding
	// weapons, a shield or a torch (Sleep In Bed's "stay dressed" choice leaves all gear on), they are
	// taken off, and put back on when that actor gets up.
	void Install();

	// Before a save is loaded (or a new game starts): what was remembered is dropped.
	void OnLoadingGame();
}
