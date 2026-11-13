#pragma once

namespace Door
{
	struct Assigned
	{
		RE::Actor*         actor;
		RE::TESObjectREFR* bed;
	};

	// Called at the end of Sleep In Bed's bedtime (player just lay down in a_playerBed).
	// a_closer: follower who closes the door (bed partner, else first follower sent to bed), may be null.
	void Begin(RE::TESObjectREFR* a_playerBed, RE::Actor* a_closer, RE::TESObjectREFR* a_closerBed, const std::vector<Assigned>& a_assigned);

	// True while this follower is walking to / closing the door (Sleep In Bed must not resend her to bed).
	bool OnDuty(RE::Actor* a_actor);

	// True when this package is one of ours that must be kept (package selection guard).
	bool KeepsPackage(RE::Actor* a_actor, RE::TESPackage* a_package);

	void Install();
}
