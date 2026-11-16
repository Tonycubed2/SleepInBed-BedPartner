#pragma once

namespace Spoon
{
	// Double beds: Double Bed Spooning - OAR (CommonDominator, Nexus 118088) + the "Double Bed Spooning patch"
	// (SleepInBedPartner - Spooning.esp and its OAR configs). The player and the bed partner get the same
	// pose marker ability, and the patched OAR configs only play that pose's animations for actors with it.
	bool        DoubleBedAvailable();
	const char* DoubleBedMissing();  // why it is not available ("" when it is)

	// Single beds and bedrolls: Spooning of Skyrim (Nexus 153263). Its hidden bed spot is placed next to
	// the player and the bed partner is sent there.
	bool        SingleBedAvailable();
	const char* SingleBedMissing();

	// Bedtime (game main thread). The player lies in a_playerBed and a_partner was given its free side.
	void PairInDoubleBed(RE::Actor* a_partner);

	// The player lies in a bed nobody shares: places Spooning of Skyrim's spot next to the player and returns
	// it (nullptr when the option is off, the mod is missing, the bed is not one it supports, or Spooning of
	// Skyrim's own spooner assignment is in use). The caller sends a_partner there.
	RE::TESObjectREFR* PlaceSingleBedSpot(RE::TESObjectREFR* a_playerBed);

	// The follower sent to the player's bed changed (hand-over to the next matching follower).
	void PartnerChanged(RE::Actor* a_old, RE::Actor* a_new);

	void Install();
	void OnLoadingGame();
}
