#include "PCH.h"

#include "Settings.h"

#include <SimpleIni.h>

namespace Settings
{
	namespace
	{
		constexpr auto kPath = "Data/SKSE/Plugins/SleepInBedPartner.ini";
		Values         g_values;

		void Sanitize(Values& v)
		{
			if (v.bedPartner < kPartnerAny || v.bedPartner > kPartnerSameSex) {
				v.bedPartner = kPartnerAny;
			}
		}
	}

	const Values& Get() { return g_values; }
	Values&       Edit() { return g_values; }

	void Load()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kPath) < 0) {
			SKSE::log::info("[settings] {} not found, using defaults", kPath);
		}
		Values loaded;
		loaded.bedPartner = static_cast<std::int32_t>(ini.GetLongValue("BedPartner", "iBedPartner", loaded.bedPartner));
		loaded.thirdPersonInBed = ini.GetBoolValue("Camera", "bThirdPersonInBed", loaded.thirdPersonInBed);
		Sanitize(loaded);
		g_values = loaded;
		SKSE::log::info("[settings] bedPartner={} thirdPersonInBed={}", PartnerName(g_values.bedPartner), g_values.thirdPersonInBed);
	}

	void Save()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kPath);
		ini.SetLongValue("BedPartner", "iBedPartner", g_values.bedPartner, "; Who may share the player's bed (Sleep In Bed SKSE: \"Followers share the player's bed\" must be on).\n; 0 = any follower, 1 = opposite sex only, 2 = same sex only. Several qualify: one is picked at random.");
		ini.SetBoolValue("Camera", "bThirdPersonInBed", g_values.thirdPersonInBed, "; 1 = while you lie in bed, the camera stays in third person (switched back if anything puts it in first person).");
		if (ini.SaveFile(kPath) < 0) {
			SKSE::log::warn("[settings] could not save {}", kPath);
		} else {
			SKSE::log::info("[settings] saved: bedPartner={} thirdPersonInBed={}", PartnerName(g_values.bedPartner), g_values.thirdPersonInBed);
		}
	}

	const char* PartnerName(std::int32_t rule)
	{
		switch (rule) {
		case kPartnerOppositeSex:
			return "opposite sex";
		case kPartnerSameSex:
			return "same sex";
		default:
			return "any follower";
		}
	}
}
