#include "PCH.h"

// Sleep In Bed SKSE - Bed Partner patch, by tonycubed2.
// Requires Sleep In Bed SKSE 1.4 by Elzar125 (Nexus 189803). Does not replace it:
// it hooks three of its functions so you choose who shares your bed:
// any follower / opposite sex / same sex, one picked at random when several qualify.
//
// Log: Documents\My Games\Skyrim Special Edition\SKSE\SleepInBedPartner.log
// INI: Data\SKSE\Plugins\SleepInBedPartner.ini

#include "Menu.h"
#include "Patch.h"
#include "Settings.h"

SKSEPluginInfo(
	.Version = REL::Version{ 1, 0, 0, 0 },
	.Name = "SleepInBedPartner"sv,
	.Author = "tonycubed2"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

namespace
{
	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg && a_msg->type == SKSE::MessagingInterface::kPostLoad) {
			// Every SKSE plugin, SleepInBedSKSE.dll included, is loaded by now.
			Patch::Install();
			Menu::Install();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	spdlog::set_level(spdlog::level::info);
	spdlog::flush_on(spdlog::level::info);
	SKSE::log::info("Sleep In Bed - Bed Partner 1.0.0 loading; game runtime {}", REL::Module::get().version().string());

	Settings::Load();

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		SKSE::log::critical("Could not register the SKSE messaging listener");
		return false;
	}
	return true;
}
