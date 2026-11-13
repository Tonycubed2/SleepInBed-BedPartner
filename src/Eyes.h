#pragma once

namespace Eyes
{
	// After sleeping, makes sure the sleeper's eyes really open again.
	void Install();
	void Track(RE::Actor* a_actor);  // a follower Sleep In Bed sends to bed (the player is always tracked)
}
