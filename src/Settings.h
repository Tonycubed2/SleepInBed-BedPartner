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
	};

	const Values& Get();
	Values&       Edit();

	void Load();
	void Save();

	const char* PartnerName(std::int32_t rule);
}
