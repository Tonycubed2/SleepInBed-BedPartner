#include "PCH.h"

#include "Settings.h"

#include <SimpleIni.h>

namespace
{
	constexpr auto kDefaultFollowerFactions = "SofiaFollower.esp|0x060480";
	bool                              g_factionsResolved = false;
	std::vector<RE::TESFaction*>      g_extraFactions;  // resolved sFollowerFactions
}

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
		loaded.redressDelay = std::clamp(static_cast<std::int32_t>(ini.GetLongValue("Dress", "iRedressDelay", loaded.redressDelay)), 0, 240);
		loaded.clearRoom = ini.GetBoolValue("Room", "bClearRoom", loaded.clearRoom);
		loaded.unequipWeaponsDressed = ini.GetBoolValue("Dress", "bUnequipWeaponsDressed", loaded.unequipWeaponsDressed);
		loaded.stayInBedToTalk = ini.GetBoolValue("Room", "bStayInBedToTalk", loaded.stayInBedToTalk);
		loaded.spoonDoubleBeds = ini.GetBoolValue("Spooning", "bDoubleBeds", loaded.spoonDoubleBeds);
		loaded.spoonPoses = static_cast<std::uint32_t>(ini.GetLongValue("Spooning", "iPoses", loaded.spoonPoses)) & 7u;
		loaded.shareSingleBeds = ini.GetBoolValue("Spooning", "bShareSingleBeds", loaded.shareSingleBeds);
		{
			std::string list = ini.GetValue("Sleepwear", "sKeepSlots", "");
			std::string number;
			for (char c : list + ",") {
				if (std::isdigit(static_cast<unsigned char>(c))) {
					number.push_back(c);
				} else if (!number.empty()) {
					const int slot = std::atoi(number.c_str());
					if (slot >= 30 && slot <= 61) {
						loaded.keepSlots |= 1u << (slot - 30);
					}
					number.clear();
				}
			}
		}
		{
			// Custom followers with their own follower faction (Sofia: SofiaFollower.esp, SofiaFollowerFaction 060480).
			std::string list = ini.GetValue("BedPartner", "sFollowerFactions", kDefaultFollowerFactions);
			std::string entry;
			for (char c : list + ",") {
				if (c == ',') {
					while (!entry.empty() && std::isspace(static_cast<unsigned char>(entry.back()))) {
						entry.pop_back();
					}
					if (!entry.empty()) {
						loaded.followerFactions.push_back(entry);
					}
					entry.clear();
				} else if (!entry.empty() || !std::isspace(static_cast<unsigned char>(c))) {
					entry.push_back(c);
				}
			}
		}
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
		g_factionsResolved = false;
		for (const auto& entry : g_values.followerFactions) {
			SKSE::log::info("[settings] follower faction: {}", entry);
		}
		SKSE::log::info("[settings] clearRoom={} notFollowers={} keepSlots={:08X} redressDelay={} min unequipWeaponsDressed={} stayInBedToTalk={}", g_values.clearRoom, g_values.notFollowers.size(), g_values.keepSlots, g_values.redressDelay, g_values.unequipWeaponsDressed, g_values.stayInBedToTalk);
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
		if (!ini.GetValue("BedPartner", "sFollowerFactions")) {
			ini.SetValue("BedPartner", "sFollowerFactions", kDefaultFollowerFactions, "; Own follower factions of custom followers that are not in the game's follower faction (comma separated, Plugin.esp|0xID).\n; Members count as followers and are sent to bed even while their own scripts switch the teammate flag off (for example while they sandbox).\n; Default: Sofia (SofiaFollower.esp|0x060480).");
		}
		if (!ini.GetValue("BedPartner", "sNotFollowers")) {
			ini.SetValue("BedPartner", "sNotFollowers", "", "; Names (comma separated) of NPCs the game counts as your teammates but who are not your followers.\n; Sleep In Bed ignores them: never sent to bed, never chosen for your bed. Example: sNotFollowers=Vilja");
		}
		{
			std::string slots;
			for (int n = 0; n < 32; ++n) {
				if (g_values.keepSlots & (1u << n)) {
					slots += (slots.empty() ? "" : ",") + std::to_string(30 + n);
				}
			}
			ini.SetValue("Sleepwear", "sKeepSlots", slots.c_str(), "; Sleepwear mode: equipment slots that stay on when the sleepwear is put on (comma separated slot numbers 30-61).\n; Empty = everything that is not sleepwear comes off (Sleep In Bed's normal behavior). Example: sKeepSlots=35,36 keeps amulets and rings.");
		}
		ini.SetLongValue("Dress", "iRedressDelay", g_values.redressDelay, "; Game minutes to stay in sleepwear after getting up before the gear goes back on (0 = at once, Sleep In Bed's normal behavior; max 240).\n; In combat, or if the player is attacked, the gear goes back on at once.");
		ini.SetBoolValue("Dress", "bUnequipWeaponsDressed", g_values.unequipWeaponsDressed, "; 1 = when you or a follower lie in bed still dressed (Sleep In Bed: stay dressed), weapons, shield and torch are taken off, and put back on when getting up.");
		ini.SetBoolValue("Room", "bStayInBedToTalk", g_values.stayInBedToTalk, "; 1 = while you lie in bed, talking to a follower who lies in bed does not make her get up.");
		ini.SetBoolValue("Spooning", "bDoubleBeds", g_values.spoonDoubleBeds, "; 1 = with Double Bed Spooning - OAR and the Double Bed Spooning patch: you and your bed partner get the same spooning pose in a double bed.");
		ini.SetLongValue("Spooning", "iPoses", static_cast<long>(g_values.spoonPoses), "; Poses that can be picked (added up): 1 = Position 3 (side by side, facing), 2 = Position 4 (arm around, resting on shoulder), 4 = Position 6 (on back, facing). 7 = all.");
		ini.SetBoolValue("Spooning", "bShareSingleBeds", g_values.shareSingleBeds, "; 1 = with Spooning of Skyrim: your bed partner also shares single beds and bedrolls (lies next to you).");
		ini.SetBoolValue("Room", "bClearRoom", g_values.clearRoom, "; 1 = when you lie down indoors, other NPCs leave the room and a follower closes the door (and closes it again if it is opened while you are in bed).");
		if (ini.SaveFile(kPath) < 0) {
			SKSE::log::warn("[settings] could not save {}", kPath);
		} else {
			SKSE::log::info("[settings] saved: bedPartner={} thirdPersonInBed={} redressPlayer={} redressFollowers={}", PartnerName(g_values.bedPartner), g_values.thirdPersonInBed, g_values.redressPlayer, g_values.redressFollowers);
		}
	}

	const std::vector<SlotGroup>& SlotGroups()
	{
		static const std::vector<SlotGroup> groups{
			{ "Head (helmets, hoods)", (1u << 0) | (1u << 1) | (1u << 11) },  // 30 head, 31 hair, 41 long hair
			{ "Body (armor, clothes)", 1u << 2 },                              // 32
			{ "Hands (gloves, gauntlets)", 1u << 3 },                          // 33
			{ "Forearms", 1u << 4 },                                           // 34
			{ "Amulet", 1u << 5 },                                             // 35
			{ "Rings", 1u << 6 },                                              // 36
			{ "Feet (boots, shoes)", 1u << 7 },                                // 37
			{ "Calves", 1u << 8 },                                             // 38
			{ "Shield", 1u << 9 },                                             // 39
			{ "Circlet", 1u << 12 },                                           // 42
			{ "Ears", 1u << 13 },                                              // 43
			{ "Mod slots 44-61 (capes, backpacks, jewelry...)", 0xFFFFC000u }, // 44-61
		};
		return groups;
	}

	bool IsExtraFollower(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		if (!g_factionsResolved) {
			g_factionsResolved = true;
			g_extraFactions.clear();
			auto* data = RE::TESDataHandler::GetSingleton();
			for (const auto& entry : g_values.followerFactions) {
				const auto bar = entry.find('|');
				if (!data || bar == std::string::npos) {
					SKSE::log::warn("[settings] follower faction \"{}\": not Plugin.esp|0xID", entry);
					continue;
				}
				const auto plugin = entry.substr(0, bar);
				const auto id = static_cast<RE::FormID>(std::strtoul(entry.c_str() + bar + 1, nullptr, 16)) & 0x00FFFFFF;
				if (auto* faction = data->LookupForm<RE::TESFaction>(id, plugin)) {
					g_extraFactions.push_back(faction);
					SKSE::log::info("[settings] follower faction {:08X} ({}) from {}", faction->GetFormID(), faction->GetName(), plugin);
				} else {
					SKSE::log::info("[settings] follower faction {}: not loaded (mod not installed)", entry);
				}
			}
		}
		for (auto* faction : g_extraFactions) {
			if (a_actor->IsInFaction(faction)) {
				return true;
			}
		}
		return false;
	}

	bool IsNotFollower(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		if (IsExtraFollower(a_actor)) {
			return false;
		}
		// A teammate who is not even a member of the vanilla CurrentFollowerFaction is not a real follower
		// (Vilja before her quest is accepted: the game flags her as teammate, rank -2 = not a member).
		// Real followers seen: Herika rank 1, Willow 0, Inigo -1; accepting Vilja's quest adds her through DialogueFollower.
		// Followers with their own follow system, like Serana's (Ashe - Crystal Heart copies it), never join
		// CurrentFollowerFaction: their script makes them a teammate and adds them to WIFollowerCommentFaction
		// (000750B8) while they follow. That membership marks them as real followers (Vilja's scripts never use it).
		static auto* currentFollower = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E);
		static auto* followerComment = RE::TESForm::LookupByID<RE::TESFaction>(0x000750B8);
		if (currentFollower && a_actor->IsPlayerTeammate() && a_actor->GetFactionRank(currentFollower, false) < -1 &&
			!(followerComment && a_actor->IsInFaction(followerComment))) {
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
