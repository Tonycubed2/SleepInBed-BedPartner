#pragma once

namespace Talk
{
	// "Followers stay in bed when you talk to them": while the player lies in bed, the beds that followers
	// lie in lose the furniture flag "Must Exit to Talk", so talking to them does not make them get up.
	// The flag is put back as soon as the player gets up.
	void Install();

	// Before a save is loaded (or a new game starts): every changed bed gets its flag back.
	void OnLoadingGame();
}
