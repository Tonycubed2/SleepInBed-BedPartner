#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Settings
{
	// Who may take the free side of the player's bed.
	enum BedPartner : std::int32_t
	{
		kPartnerAny = 0,
		kPartnerOppositeSex = 1,
		kPartnerSameSex = 2,
	};

	struct Values
	{
		std::int32_t bedPartner{ kPartnerAny };
		bool         thirdPersonInBed{ false };
		bool         redressPlayer{ true };     // put the player's gear back on after sleeping
		bool         redressFollowers{ true };  // put followers' gear back on after sleeping
		std::int32_t redressDelay{ 0 };         // game minutes to stay in sleepwear before the gear goes back on (0 = at once)
		bool         clearRoom{ false };        // NPCs leave the room, a follower closes the door
		std::vector<std::string> notFollowers;  // names Sleep In Bed must not treat as followers (lower case)
		std::uint32_t keepSlots{ 0 };           // sleepwear mode: biped slots kept on (bit n = slot 30+n)
		bool         unequipWeaponsDressed{ false };  // "stay dressed": weapons, shield and torch still come off in bed
	};

	const Values& Get();
	Values&       Edit();

	void Load();
	void Save();

	const char* PartnerName(std::int32_t rule);

	// True when this actor's name is on the "not a follower" list.
	bool IsNotFollower(RE::Actor* a_actor);

	// Slot groups shown in the menu: name and biped slot mask (bit n = slot 30+n).
	struct SlotGroup
	{
		const char*   name;
		std::uint32_t mask;
	};
	const std::vector<SlotGroup>& SlotGroups();
}
