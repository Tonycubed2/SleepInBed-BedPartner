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
		loaded.redressPlayer = ini.GetBoolValue("Dress", "bRedressPlayer", loaded.redressPlayer);
		loaded.redressFollowers = ini.GetBoolValue("Dress", "bRedressFollowers", loaded.redressFollowers);
		loaded.clearRoom = ini.GetBoolValue("Room", "bClearRoom", loaded.clearRoom);
		{
			std::string list = ini.GetValue("BedPartner", "sNotFollowers", "");
			std::string name;
			for (char c : list + ",") {
				if (c == ',') {
					while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
						name.pop_back();
					}
					if (!name.empty()) {
						loaded.notFollowers.push_back(name);
					}
					name.clear();
				} else if (!name.empty() || !std::isspace(static_cast<unsigned char>(c))) {
					name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
				}
			}
		}
		Sanitize(loaded);
		g_values = loaded;
		SKSE::log::info("[settings] clearRoom={} notFollowers={}", g_values.clearRoom, g_values.notFollowers.size());
		for (const auto& name : g_values.notFollowers) {
			SKSE::log::info("[settings] not a follower: {}", name);
		}
		SKSE::log::info("[settings] bedPartner={} thirdPersonInBed={} redressPlayer={} redressFollowers={}", PartnerName(g_values.bedPartner), g_values.thirdPersonInBed, g_values.redressPlayer, g_values.redressFollowers);
	}

	void Save()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kPath);
		ini.SetLongValue("BedPartner", "iBedPartner", g_values.bedPartner, "; Who may share the player's bed (Sleep In Bed SKSE: \"Followers share the player's bed\" must be on).\n; 0 = any follower, 1 = opposite sex only, 2 = same sex only. Several qualify: one is picked at random.");
		ini.SetBoolValue("Camera", "bThirdPersonInBed", g_values.thirdPersonInBed, "; 1 = while you lie in bed, the camera stays in third person (switched back if anything puts it in first person).");
		ini.SetBoolValue("Dress", "bRedressPlayer", g_values.redressPlayer, "; 1 = after sleeping, your gear is put back on (Sleep In Bed's normal behavior). 0 = you stay as you are.");
		ini.SetBoolValue("Dress", "bRedressFollowers", g_values.redressFollowers, "; 1 = after sleeping, followers' gear is put back on (Sleep In Bed's normal behavior). 0 = they stay as they are.");
		if (!ini.GetValue("BedPartner", "sNotFollowers")) {
			ini.SetValue("BedPartner", "sNotFollowers", "", "; Names (comma separated) of NPCs the game counts as your teammates but who are not your followers.\n; Sleep In Bed ignores them: never sent to bed, never chosen for your bed. Example: sNotFollowers=Vilja");
		}
		ini.SetBoolValue("Room", "bClearRoom", g_values.clearRoom, "; 1 = when you lie down indoors, other NPCs leave the room and a follower closes the door (and closes it again if it is opened while you are in bed).");
		if (ini.SaveFile(kPath) < 0) {
			SKSE::log::warn("[settings] could not save {}", kPath);
		} else {
			SKSE::log::info("[settings] saved: bedPartner={} thirdPersonInBed={} redressPlayer={} redressFollowers={}", PartnerName(g_values.bedPartner), g_values.thirdPersonInBed, g_values.redressPlayer, g_values.redressFollowers);
		}
	}

	bool IsNotFollower(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		// A teammate who is not even a member of the vanilla CurrentFollowerFaction is not a real follower
		// (Vilja before her quest is accepted: the game flags her as teammate, rank -2 = not a member).
		// Real followers seen: Herika rank 1, Willow 0, Inigo -1; accepting Vilja's quest adds her through DialogueFollower.
		static auto* currentFollower = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E);
		if (currentFollower && a_actor->IsPlayerTeammate() && a_actor->GetFactionRank(currentFollower, false) < -1) {
			return true;
		}
		if (g_values.notFollowers.empty()) {
			return false;
		}
		const char* raw = a_actor->GetDisplayFullName();
		if (!raw || !*raw) {
			return false;
		}
		std::string name = raw;
		for (auto& c : name) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return std::find(g_values.notFollowers.begin(), g_values.notFollowers.end(), name) != g_values.notFollowers.end();
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
