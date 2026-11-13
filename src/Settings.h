#pragma once

#include <cstdint>

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
		bool         clearRoom{ false };        // NPCs leave the room, a follower closes the door
	};

	const Values& Get();
	Values&       Edit();

	void Load();
	void Save();

	const char* PartnerName(std::int32_t rule);
}
