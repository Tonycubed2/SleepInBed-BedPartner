#include "PCH.h"

#include "Patch.h"

#include "Settings.h"

#include <MinHook.h>
#include <Windows.h>

// How Sleep In Bed SKSE 1.4 picks who shares the player's bed (from its source, Followers.cpp,
// confirmed against the disassembly of the released SleepInBedSKSE.dll 1.4):
//
//   LayFollowersDown(bedHandle)              RVA 0x27B30
//     CollectSleepers (inlined): followers near the player, then
//       std::sort(others, by distance)       RVA 0x25450  (only used for this list)
//     spouse:    AllowedToUse(spouse,   playerBed)   RVA 0x27370
//     followers: for each follower in sorted order:
//                  if share && free side && AllowedToUse(follower, playerBed) -> player's bed
//                  else PickBed(other beds; the player's bed is never in that list)
//
// So the nearest follower always gets the free side. This patch:
//   - after the sort, moves the followers that match the chosen rule to the front, in random order;
//   - makes AllowedToUse say "no" for the player's bed to every follower that does not match.
// Nothing else in Sleep In Bed SKSE changes. The spouse is never touched.

namespace Patch
{
	namespace
	{
		constexpr std::uint32_t kSupportedTimeDateStamp = 1790422001;  // SleepInBedSKSE.dll 1.4, 2026-09-26

		constexpr std::uintptr_t kRvaLayFollowersDown = 0x27B30;
		constexpr std::uintptr_t kRvaSortSleepers = 0x25450;
		constexpr std::uintptr_t kRvaAllowedToUse = 0x27370;

		constexpr std::array<std::uint8_t, 16> kBytesLayFollowersDown{ 0x48, 0x8B, 0xC4, 0x89, 0x48, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56 };
		constexpr std::array<std::uint8_t, 16> kBytesSortSleepers{ 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x4C, 0x89, 0x48, 0x20, 0x48, 0x89, 0x50, 0x10, 0x55 };
		constexpr std::array<std::uint8_t, 16> kBytesAllowedToUse{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };

		using LayFollowersDown_t = void (*)(std::uint32_t a_bedHandle);
		using SortSleepers_t = void (*)(RE::Actor** a_first, RE::Actor** a_last, std::int64_t a_ideal, void* a_pred);
		using AllowedToUse_t = bool (*)(RE::Actor* a_actor, RE::TESObjectREFR* a_bed, bool a_childBed, RE::PlayerCharacter* a_player);

		LayFollowersDown_t g_origLayFollowersDown = nullptr;
		SortSleepers_t     g_origSortSleepers = nullptr;
		AllowedToUse_t     g_origAllowedToUse = nullptr;

		State g_state = State::kNotInstalled;

		// Valid only while LayFollowersDown runs (game main thread).
		bool                             g_inBedtime = false;
		int                              g_sortDepth = 0;
		RE::TESObjectREFR*               g_playerBed = nullptr;
		std::unordered_set<RE::Actor*>   g_followers;
		std::unordered_set<RE::Actor*>   g_allowed;
		std::unordered_set<RE::Actor*>   g_loggedBlocked;

		std::mt19937& Random()
		{
			static std::mt19937 engine{ std::random_device{}() };
			return engine;
		}

		// Sex as the game sees it: the female flag of the actor's base record
		// (same source as the GetIsSex condition and ActorBase.GetSex()).
		bool SexOf(RE::Actor* a_actor, RE::SEX& a_sex)
		{
			auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
			if (!base) {
				return false;
			}
			a_sex = base->GetSex();
			return true;
		}

		bool Matches(RE::Actor* a_actor, RE::PlayerCharacter* a_player, std::int32_t a_rule)
		{
			if (a_rule != Settings::kPartnerOppositeSex && a_rule != Settings::kPartnerSameSex) {
				return true;
			}
			RE::SEX actorSex{};
			RE::SEX playerSex{};
			if (!SexOf(a_actor, actorSex) || !SexOf(a_player, playerSex)) {
				return false;
			}
			const bool same = actorSex == playerSex;
			return a_rule == Settings::kPartnerSameSex ? same : !same;
		}

		void ResetBedtime()
		{
			g_inBedtime = false;
			g_sortDepth = 0;
			g_playerBed = nullptr;
			g_followers.clear();
			g_allowed.clear();
			g_loggedBlocked.clear();
		}

		void HookLayFollowersDown(std::uint32_t a_bedHandle)
		{
			ResetBedtime();
			RE::NiPointer<RE::TESObjectREFR> bed;
			RE::TESObjectREFR::LookupByHandle(a_bedHandle, bed);
			g_playerBed = bed.get();
			g_inBedtime = g_playerBed != nullptr;
			g_origLayFollowersDown(a_bedHandle);
			ResetBedtime();
		}

		void Reorder(RE::Actor** a_first, RE::Actor** a_last)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || a_first == a_last) {
				return;
			}
			const auto rule = Settings::Get().bedPartner;

			std::vector<RE::Actor*> matching;
			std::vector<RE::Actor*> rest;
			for (auto** it = a_first; it != a_last; ++it) {
				auto* actor = *it;
				if (!actor) {
					rest.push_back(actor);
					continue;
				}
				g_followers.insert(actor);
				if (Matches(actor, player, rule)) {
					matching.push_back(actor);
				} else {
					rest.push_back(actor);
				}
			}
			std::shuffle(matching.begin(), matching.end(), Random());
			g_allowed.insert(matching.begin(), matching.end());

			auto* out = a_first;
			for (auto* actor : matching) {
				*out++ = actor;
			}
			for (auto* actor : rest) {
				*out++ = actor;
			}

			if (matching.empty()) {
				SKSE::log::info("[partner] {} follower(s) nearby, none qualifies for the player's bed ({})", g_followers.size(), Settings::PartnerName(rule));
			} else {
				SKSE::log::info("[partner] {:08X} picked at random from {} of {} follower(s) for the player's bed ({})", matching.front()->GetFormID(), matching.size(), g_followers.size(), Settings::PartnerName(rule));
			}
		}

		void HookSortSleepers(RE::Actor** a_first, RE::Actor** a_last, std::int64_t a_ideal, void* a_pred)
		{
			// std::sort recurses into itself: only the outermost call is the finished sort.
			++g_sortDepth;
			g_origSortSleepers(a_first, a_last, a_ideal, a_pred);
			--g_sortDepth;
			if (g_sortDepth == 0 && g_inBedtime) {
				Reorder(a_first, a_last);
			}
		}

		bool HookAllowedToUse(RE::Actor* a_actor, RE::TESObjectREFR* a_bed, bool a_childBed, RE::PlayerCharacter* a_player)
		{
			if (g_inBedtime && a_bed && a_bed == g_playerBed && g_followers.contains(a_actor) && !g_allowed.contains(a_actor)) {
				if (g_loggedBlocked.insert(a_actor).second) {
					SKSE::log::info("[partner] {:08X} may not share the player's bed ({}), goes to another bed", a_actor->GetFormID(), Settings::PartnerName(Settings::Get().bedPartner));
				}
				return false;
			}
			return g_origAllowedToUse(a_actor, a_bed, a_childBed, a_player);
		}

		bool BytesMatch(std::uintptr_t a_base, std::uintptr_t a_rva, const std::array<std::uint8_t, 16>& a_expected, const char* a_name)
		{
			const auto* code = reinterpret_cast<const std::uint8_t*>(a_base + a_rva);
			if (std::memcmp(code, a_expected.data(), a_expected.size()) != 0) {
				SKSE::log::warn("[patch] {} at +{:X} does not match Sleep In Bed SKSE 1.4", a_name, a_rva);
				return false;
			}
			return true;
		}

		bool Hook(std::uintptr_t a_base, std::uintptr_t a_rva, void* a_detour, void** a_original, const char* a_name)
		{
			auto* target = reinterpret_cast<void*>(a_base + a_rva);
			if (MH_CreateHook(target, a_detour, a_original) != MH_OK) {
				SKSE::log::error("[patch] could not create the {} hook", a_name);
				return false;
			}
			return true;
		}
	}

	void Install()
	{
		auto* module = GetModuleHandleW(L"SleepInBedSKSE.dll");
		if (!module) {
			g_state = State::kMissing;
			SKSE::log::warn("[patch] SleepInBedSKSE.dll is not loaded: install Sleep In Bed SKSE 1.4. Nothing patched.");
			return;
		}
		const auto base = reinterpret_cast<std::uintptr_t>(module);

		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		const auto  stamp = nt->FileHeader.TimeDateStamp;
		const bool  bytesOk = BytesMatch(base, kRvaLayFollowersDown, kBytesLayFollowersDown, "LayFollowersDown") &
		                     BytesMatch(base, kRvaSortSleepers, kBytesSortSleepers, "SortSleepers") &
		                     BytesMatch(base, kRvaAllowedToUse, kBytesAllowedToUse, "AllowedToUse");
		if (stamp != kSupportedTimeDateStamp || !bytesOk) {
			g_state = State::kWrongVersion;
			SKSE::log::warn("[patch] SleepInBedSKSE.dll build stamp {} is not Sleep In Bed SKSE 1.4 ({}). Nothing patched.", stamp, kSupportedTimeDateStamp);
			return;
		}

		const auto init = MH_Initialize();
		if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
			g_state = State::kHookFailed;
			SKSE::log::error("[patch] MinHook could not start");
			return;
		}
		const bool created = Hook(base, kRvaLayFollowersDown, reinterpret_cast<void*>(&HookLayFollowersDown), reinterpret_cast<void**>(&g_origLayFollowersDown), "LayFollowersDown") &&
		                     Hook(base, kRvaSortSleepers, reinterpret_cast<void*>(&HookSortSleepers), reinterpret_cast<void**>(&g_origSortSleepers), "SortSleepers") &&
		                     Hook(base, kRvaAllowedToUse, reinterpret_cast<void*>(&HookAllowedToUse), reinterpret_cast<void**>(&g_origAllowedToUse), "AllowedToUse");
		if (!created || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
			MH_DisableHook(MH_ALL_HOOKS);
			g_state = State::kHookFailed;
			SKSE::log::error("[patch] hooks could not be enabled. Nothing patched.");
			return;
		}
		g_state = State::kActive;
		SKSE::log::info("[patch] Sleep In Bed SKSE 1.4 patched (module at {:X})", base);
	}

	State GetState()
	{
		return g_state;
	}
}
