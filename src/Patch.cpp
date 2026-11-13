#include "PCH.h"

#include "Patch.h"

#include "Door.h"
#include "Eyes.h"
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
//   Assign(actor, bed, slot, role)            RVA 0x27400  (undress, send to bed, track; slot may be null)
//   SelectPackage(process, actor, force)      RVA 0x39EA0  (its detour on the game's package selection:
//                                                           keeps the sleep package only once the follower
//                                                           is lying down, sit/sleep state >= 5)
//
// Why followers like CHIM's Herika never arrive: while they WALK to the bed, every package re-evaluation
// picks their quest alias follow package (Skyrim.esm 0005C84B, used by HerikaFollower and DialogueFollower),
// which outranks the created sleep package. Sleep In Bed resends it every ~3 s, up to 5 times, then gives up.
//
// So the nearest follower always gets the free side. This patch:
//   - after the sort, moves the followers that match the chosen rule to the front, in random order;
//   - makes AllowedToUse say "no" for the player's bed to every follower that does not match;
//   - watches the follower sent to the player's bed: when her own AI takes her sleep package away
//     twice (Sleep In Bed keeps resending it), or she never starts it, the next matching follower is
//     sent to the player's bed through Sleep In Bed's own Assign;
//   - while the player lies in bed, also keeps the sleep package of followers still walking to their bed
//     (Sleep In Bed itself only does so once they lie down), so their follow package cannot take it back.
// Nothing else in Sleep In Bed SKSE changes. The spouse is never touched.

namespace Patch
{
	namespace
	{
		constexpr std::uint32_t kSupportedTimeDateStamp = 1790422001;  // SleepInBedSKSE.dll 1.4, 2026-09-26

		constexpr std::uintptr_t kRvaLayFollowersDown = 0x27B30;
		constexpr std::uintptr_t kRvaSortSleepers = 0x25450;
		constexpr std::uintptr_t kRvaAllowedToUse = 0x27370;
		constexpr std::uintptr_t kRvaAssign = 0x27400;
		constexpr std::uintptr_t kRvaSelectPackage = 0x39EA0;
		constexpr std::uintptr_t kRvaDressNow = 0x4F8C0;      // Undress::DressNow(actorID, animate): the only re-dress path
		constexpr std::uintptr_t kRvaPutOn = 0x51370;         // Undress::PutOn(actor, itemID, slot)
		constexpr std::uintptr_t kRvaUnequip = 0x69870;       // ActorEquipManager::UnequipObject wrapper (sleepwear doff)
		constexpr std::uintptr_t kRvaPartOfOutfit = 0x46500;        // Sleepwear::PartOfOutfit(actor, armor): sleepwear mode "keep this on?"
		constexpr std::uintptr_t kRvaSendSleepPackage = 0x39FF0;    // SleepPackage::Send(actor, bed): every (re)send to bed
		constexpr std::uintptr_t kRvaCanProcessSleepKey = 0x2ACC0;  // its MenuOpenHandler::CanProcess replacement
		constexpr std::uintptr_t kRvaIsSleepKey = 0x2AF50;          // bool IsSleepKey(const ButtonEvent&)
		constexpr std::uintptr_t kRvaVanillaCanProcess = 0x11A268;  // where it keeps the game's own CanProcess

		constexpr std::array<std::uint8_t, 16> kBytesLayFollowersDown{ 0x48, 0x8B, 0xC4, 0x89, 0x48, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56 };
		constexpr std::array<std::uint8_t, 16> kBytesSortSleepers{ 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x4C, 0x89, 0x48, 0x20, 0x48, 0x89, 0x50, 0x10, 0x55 };
		constexpr std::array<std::uint8_t, 16> kBytesAllowedToUse{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
		constexpr std::array<std::uint8_t, 16> kBytesSelectPackage{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x44, 0x88, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC };
		constexpr std::array<std::uint8_t, 16> kBytesDressNow{ 0x88, 0x54, 0x24, 0x10, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55 };
		constexpr std::array<std::uint8_t, 16> kBytesPutOn{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
		constexpr std::array<std::uint8_t, 16> kBytesUnequip{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
		constexpr std::array<std::uint8_t, 16> kBytesCanProcessSleepKey{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9 };
		constexpr std::array<std::uint8_t, 16> kBytesIsSleepKey{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x9E, 0x79 };
		constexpr std::array<std::uint8_t, 16> kBytesPartOfOutfit{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xDA, 0x48, 0x85, 0xC9, 0x74, 0x57, 0x48, 0x85 };
		constexpr std::array<std::uint8_t, 16> kBytesSendSleepPackage{ 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD9 };
		constexpr std::array<std::uint8_t, 16> kBytesAssign{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x4C };

		// Hand-over: losses of the sleep package before the next follower gets the bed,
		// seconds a follower may take to start her sleep package (undressing included),
		// seconds the watch lasts at most.
		constexpr int   kLossesBeforeHandover = 2;
		constexpr float kStartTimeout = 30.0f;
		constexpr float kWatchTimeout = 120.0f;
		constexpr auto  kRole = "follower";

		using LayFollowersDown_t = void (*)(std::uint32_t a_bedHandle);
		using SortSleepers_t = void (*)(RE::Actor** a_first, RE::Actor** a_last, std::int64_t a_ideal, void* a_pred);
		using AllowedToUse_t = bool (*)(RE::Actor* a_actor, RE::TESObjectREFR* a_bed, bool a_childBed, RE::PlayerCharacter* a_player);
		using Assign_t = void (*)(RE::Actor* a_actor, RE::TESObjectREFR* a_bed, void* a_slot, const char* a_role);
		using SelectPackage_t = bool (*)(RE::AIProcess* a_process, RE::Actor* a_actor, bool a_force);
		using DressNow_t = void (*)(RE::FormID a_actorID, bool a_animate);
		using CanProcess_t = bool (*)(RE::MenuOpenHandler* a_handler, RE::InputEvent* a_event);
		using SendSleepPackage_t = bool (*)(RE::Actor* a_actor, RE::TESObjectREFR* a_bed);
		using PartOfOutfit_t = bool (*)(RE::Actor* a_actor, RE::TESObjectARMO* a_armor);
		using IsSleepKey_t = bool (*)(const RE::ButtonEvent* a_event);
		using PutOn_t = bool (*)(RE::Actor* a_actor, RE::FormID a_itemID, std::uint8_t a_slot);
		using Unequip_t = bool (*)(RE::ActorEquipManager* a_manager, RE::Actor* a_actor, RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraData, std::uint32_t a_count, const RE::BGSEquipSlot* a_slot, bool a_queueEquip, bool a_forceEquip, bool a_playSounds, bool a_applyNow, const RE::BGSEquipSlot* a_slotToReplace);

		LayFollowersDown_t g_origLayFollowersDown = nullptr;
		SortSleepers_t     g_origSortSleepers = nullptr;
		AllowedToUse_t     g_origAllowedToUse = nullptr;
		Assign_t           g_origAssign = nullptr;
		SelectPackage_t    g_origSelectPackage = nullptr;
		DressNow_t         g_origDressNow = nullptr;
		CanProcess_t       g_origCanProcessSleepKey = nullptr;
		SendSleepPackage_t g_origSendSleepPackage = nullptr;
		PartOfOutfit_t     g_origPartOfOutfit = nullptr;
		IsSleepKey_t       g_isSleepKey = nullptr;
		CanProcess_t*      g_vanillaCanProcess = nullptr;  // slot in SleepInBedSKSE.dll holding the game's function
		PutOn_t            g_origPutOn = nullptr;
		Unequip_t          g_origUnequip = nullptr;

		// True while DressNow runs for someone who must stay as they are: nothing is put on or taken off.
		thread_local bool g_keepAsIs = false;

		// Followers whose walk to bed was protected this bedtime (package selection can run on AI threads).
		std::mutex                        g_walkLock;
		std::unordered_set<RE::FormID>    g_walkLogged;

		State g_state = State::kNotInstalled;

		// Valid only while LayFollowersDown runs (game main thread).
		bool                             g_inBedtime = false;
		int                              g_sortDepth = 0;
		RE::TESObjectREFR*               g_playerBed = nullptr;
		std::unordered_set<RE::Actor*>   g_followers;
		std::unordered_set<RE::Actor*>   g_allowed;
		std::unordered_set<RE::Actor*>   g_loggedBlocked;
		std::vector<RE::Actor*>          g_matchingOrder;  // matching followers, in the random order
		std::vector<Door::Assigned>      g_assignedList;   // everyone Sleep In Bed sent to a bed this bedtime
		RE::Actor*                       g_assignedPartner = nullptr;

		// Watch of the follower sent to the player's bed (game main thread only).
		struct Watch
		{
			bool                             active{ false };
			RE::ActorHandle                  partner;
			RE::ObjectRefHandle              bed;
			std::vector<RE::ActorHandle>     backups;
			int                              losses{ 0 };
			bool                             held{ false };
			bool                             everHeld{ false };
			std::chrono::steady_clock::time_point started;
			std::chrono::steady_clock::time_point partnerSince;
		};
		Watch             g_watch;
		std::atomic<bool> g_watchActive{ false };
		std::atomic<bool> g_tickQueued{ false };

		const char* NameOf(RE::Actor* a_actor)
		{
			const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
			return name && *name ? name : "?";
		}

		bool HoldsCreatedSleepPackage(RE::Actor* a_actor)
		{
			auto* process = a_actor ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
			auto* package = process ? process->currentPackage.package : nullptr;
			return package && package->packData.packType == RE::PACKAGE_TYPE::kSleep && package->packData.packFlags.any(RE::PACKAGE_DATA::GeneralFlag::kCreated);
		}

		bool PlayerLyingInBed()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player && player->AsActorState()->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping;
		}

		void StopWatch(const char* a_why)
		{
			if (g_watch.active) {
				SKSE::log::info("[partner] watch ended: {}", a_why);
			}
			g_watch = Watch{};
			g_watchActive = false;
		}

		float Seconds(std::chrono::steady_clock::time_point a_since)
		{
			return std::chrono::duration<float>(std::chrono::steady_clock::now() - a_since).count();
		}

		void HandOver(const char* a_why)
		{
			auto  bedPtr = g_watch.bed.get();
			auto* bed = bedPtr.get();
			auto  oldPtr = g_watch.partner.get();
			auto* old = oldPtr.get();
			if (!bed) {
				StopWatch("the player's bed is gone");
				return;
			}
			while (!g_watch.backups.empty()) {
				auto  nextPtr = g_watch.backups.front().get();
				auto* next = nextPtr.get();
				g_watch.backups.erase(g_watch.backups.begin());
				if (!next || next->IsDead() || !next->Is3DLoaded()) {
					continue;
				}
				SKSE::log::info("[partner] {:08X} ({}) {}: the player's bed goes to {:08X} ({})", old ? old->GetFormID() : 0, NameOf(old), a_why, next->GetFormID(), NameOf(next));
				g_origAssign(next, bed, nullptr, kRole);
				g_watch.partner = next->GetHandle();
				g_watch.losses = 0;
				g_watch.held = false;
				g_watch.everHeld = false;
				g_watch.partnerSince = std::chrono::steady_clock::now();
				return;
			}
			SKSE::log::info("[partner] {:08X} ({}) {}: no other matching follower left for the player's bed", old ? old->GetFormID() : 0, NameOf(old), a_why);
			StopWatch("no other matching follower");
		}

		void WatchTick()
		{
			g_tickQueued = false;
			if (!g_watch.active) {
				return;
			}
			if (!PlayerLyingInBed()) {
				StopWatch("the player got up");
				return;
			}
			if (Seconds(g_watch.started) > kWatchTimeout) {
				StopWatch("time is up");
				return;
			}
			auto  partnerPtr = g_watch.partner.get();
			auto* partner = partnerPtr.get();
			if (!partner || partner->IsDead() || !partner->Is3DLoaded()) {
				HandOver("is gone");
				return;
			}
			if (Door::OnDuty(partner)) {
				// Closing the room door: not a lost sleep package.
				g_watch.losses = 0;
				g_watch.held = false;
				g_watch.partnerSince = std::chrono::steady_clock::now();
				return;
			}
			const bool held = HoldsCreatedSleepPackage(partner);
			if (held) {
				g_watch.everHeld = true;
			} else if (g_watch.held) {
				++g_watch.losses;
				SKSE::log::info("[partner] {:08X} ({}) lost her sleep package ({}/{})", partner->GetFormID(), NameOf(partner), g_watch.losses, kLossesBeforeHandover);
			}
			g_watch.held = held;
			if (g_watch.losses >= kLossesBeforeHandover) {
				HandOver("keeps losing her sleep package");
			} else if (!g_watch.everHeld && Seconds(g_watch.partnerSince) > kStartTimeout) {
				HandOver("never started her sleep package");
			}
		}

		void WatchThread()
		{
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				if (!g_watchActive || g_tickQueued.exchange(true)) {
					continue;
				}
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([] { WatchTick(); });
				} else {
					g_tickQueued = false;
				}
			}
		}

		void StartWatch(RE::Actor* a_partner, RE::TESObjectREFR* a_bed)
		{
			StopWatch("a new bedtime started");
			g_watch.active = true;
			g_watch.partner = a_partner->GetHandle();
			g_watch.bed = a_bed->GetHandle();
			for (auto* actor : g_matchingOrder) {
				if (actor && actor != a_partner) {
					g_watch.backups.push_back(actor->GetHandle());
				}
			}
			g_watch.started = std::chrono::steady_clock::now();
			g_watch.partnerSince = g_watch.started;
			g_watchActive = true;
			SKSE::log::info("[partner] watching {:08X} ({}) on the way to the player's bed; {} other matching follower(s) in reserve", a_partner->GetFormID(), NameOf(a_partner), g_watch.backups.size());
		}

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
			g_matchingOrder.clear();
			g_assignedList.clear();
			g_assignedPartner = nullptr;
		}

		void HookLayFollowersDown(std::uint32_t a_bedHandle)
		{
			ResetBedtime();
			{
				std::scoped_lock lock(g_walkLock);
				g_walkLogged.clear();
			}
			RE::NiPointer<RE::TESObjectREFR> bed;
			RE::TESObjectREFR::LookupByHandle(a_bedHandle, bed);
			g_playerBed = bed.get();
			g_inBedtime = g_playerBed != nullptr;
			g_origLayFollowersDown(a_bedHandle);
			for (auto* follower : g_followers) {
				Eyes::Track(follower);
			}
			if (g_playerBed) {
				RE::Actor*         closer = g_assignedPartner;
				RE::TESObjectREFR* closerBed = g_assignedPartner ? g_playerBed : nullptr;
				if (!closer) {
					for (const auto& entry : g_assignedList) {
						if (g_followers.contains(entry.actor)) {
							closer = entry.actor;
							closerBed = entry.bed;
							break;
						}
					}
				}
				Door::Begin(g_playerBed, closer, closerBed, g_assignedList);
			}
			if (g_assignedPartner && g_playerBed) {
				StartWatch(g_assignedPartner, g_playerBed);
			}
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
				{
					// Diagnostics: what tells a real follower from an NPC some mod only flags as teammate.
					static auto* currentFollower = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E);
					auto*        process = actor->GetActorRuntimeData().currentProcess;
					auto*        package = process ? process->currentPackage.package : nullptr;
					SKSE::log::info("[partner] teammate {:08X} ({}): CurrentFollowerFaction rank {} | package {:08X} type {} | following player {}", actor->GetFormID(), NameOf(actor), currentFollower ? actor->GetFactionRank(currentFollower, false) : -9, package ? package->GetFormID() : 0, package ? static_cast<std::int32_t>(package->packData.packType.get()) : -1, package && package->packData.packType == RE::PACKAGE_TYPE::kFollow);
				}
				if (Settings::IsNotFollower(actor)) {
					rest.push_back(actor);
					continue;
				}
				if (Matches(actor, player, rule)) {
					matching.push_back(actor);
				} else {
					rest.push_back(actor);
				}
			}
			std::shuffle(matching.begin(), matching.end(), Random());
			g_allowed.insert(matching.begin(), matching.end());
			g_matchingOrder = matching;

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
				SKSE::log::info("[partner] {:08X} ({}) picked at random from {} of {} follower(s) for the player's bed ({})", matching.front()->GetFormID(), NameOf(matching.front()), matching.size(), g_followers.size(), Settings::PartnerName(rule));
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
					SKSE::log::info("[partner] {:08X} ({}) may not share the player's bed ({}), goes to another bed", a_actor->GetFormID(), NameOf(a_actor), Settings::PartnerName(Settings::Get().bedPartner));
				}
				return false;
			}
			return g_origAllowedToUse(a_actor, a_bed, a_childBed, a_player);
		}

		// Sleepwear mode keeps on only sleepwear pieces; items in the slots the player chose stay on too.
		bool HookPartOfOutfit(RE::Actor* a_actor, RE::TESObjectARMO* a_armor)
		{
			if (g_origPartOfOutfit(a_actor, a_armor)) {
				return true;
			}
			const auto keep = Settings::Get().keepSlots;
			if (keep && a_armor && (a_armor->GetSlotMask().underlying() & keep) != 0) {
				SKSE::log::info("[sleepwear] {:08X} ({}) keeps {:08X} ({}) on: slot chosen to stay on", a_actor ? a_actor->GetFormID() : 0, NameOf(a_actor), a_armor->GetFormID(), a_armor->GetName());
				return true;
			}
			return false;
		}

		bool HookSendSleepPackage(RE::Actor* a_actor, RE::TESObjectREFR* a_bed)
		{
			if (Door::OnDuty(a_actor)) {
				// She is closing the room door; she is sent to bed again once it is closed.
				return true;
			}
			return g_origSendSleepPackage(a_actor, a_bed);
		}

		bool HookSelectPackage(RE::AIProcess* a_process, RE::Actor* a_actor, bool a_force)
		{
			auto* package = a_process ? a_process->currentPackage.package : nullptr;
			if (Door::KeepsPackage(a_actor, package)) {
				return false;
			}
			if (a_actor && !a_actor->IsPlayerRef() && package &&
				package->packData.packType == RE::PACKAGE_TYPE::kSleep &&
				package->packData.packFlags.any(RE::PACKAGE_DATA::GeneralFlag::kCreated) &&
				a_actor->AsActorState()->GetSitSleepState() < RE::SIT_SLEEP_STATE::kWantToSleep &&
				PlayerLyingInBed()) {
				bool first = false;
				{
					std::scoped_lock lock(g_walkLock);
					first = g_walkLogged.insert(a_actor->GetFormID()).second;
				}
				if (first) {
					SKSE::log::info("[partner] {:08X} ({}) walking to bed: her sleep package is kept (force={})", a_actor->GetFormID(), NameOf(a_actor), a_force);
				}
				return false;
			}
			return g_origSelectPackage(a_process, a_actor, a_force);
		}

		// Sleep In Bed 1.4 answers "yes" for its sleep key (by default the Wait key/button) everywhere, so in
		// unpaused inventory/magic/favorites menus the back/Wait button opens the Wait menu instead of switching
		// menus. It only uses that key when the player lies in bed or has a jail sentence to serve, with no menu
		// open; everywhere else the game's own answer is used.
		bool AnyMenuUsingTheKeyOpen(RE::UI* a_ui)
		{
			static constexpr std::array<std::string_view, 21> kMenus{
				RE::InventoryMenu::MENU_NAME, RE::MagicMenu::MENU_NAME, RE::FavoritesMenu::MENU_NAME, RE::TweenMenu::MENU_NAME,
				RE::ContainerMenu::MENU_NAME, RE::BarterMenu::MENU_NAME, RE::GiftMenu::MENU_NAME, RE::MapMenu::MENU_NAME,
				RE::JournalMenu::MENU_NAME, RE::StatsMenu::MENU_NAME, RE::BookMenu::MENU_NAME, RE::LockpickingMenu::MENU_NAME,
				RE::CraftingMenu::MENU_NAME, RE::DialogueMenu::MENU_NAME, RE::SleepWaitMenu::MENU_NAME, RE::MessageBoxMenu::MENU_NAME,
				RE::Console::MENU_NAME, RE::TrainingMenu::MENU_NAME, RE::LevelUpMenu::MENU_NAME, RE::FaderMenu::MENU_NAME, RE::LoadingMenu::MENU_NAME
			};
			for (const auto& name : kMenus) {
				if (a_ui->IsMenuOpen(name)) {
					return true;
				}
			}
			return false;
		}

		bool SleepKeyNeeded()
		{
			auto* ui = RE::UI::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!ui || !player || ui->numPausesGame > 0 || AnyMenuUsingTheKeyOpen(ui)) {
				return false;
			}
			const auto life = player->AsActorState()->GetLifeState();
			if (life == RE::ACTOR_LIFE_STATE::kDying || life == RE::ACTOR_LIFE_STATE::kDead) {
				return false;
			}
			const bool lyingInBed = player->AsActorState()->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping;
			const bool sentence = player->GetPlayerRuntimeData().jailSentence > 0 && !player->GetPlayerFlags().escaping;
			return lyingInBed || sentence;
		}

		bool HookCanProcessSleepKey(RE::MenuOpenHandler* a_handler, RE::InputEvent* a_event)
		{
			if (a_event && a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
				const auto* button = a_event->AsButtonEvent();
				if (button && g_isSleepKey(button) && !SleepKeyNeeded()) {
					auto vanilla = *g_vanillaCanProcess;
					return vanilla ? vanilla(a_handler, a_event) : false;
				}
			}
			return g_origCanProcessSleepKey(a_handler, a_event);
		}

		void HookDressNow(RE::FormID a_actorID, bool a_animate)
		{
			const bool player = a_actorID == 0x14;
			const bool redress = player ? Settings::Get().redressPlayer : Settings::Get().redressFollowers;
			if (redress) {
				g_origDressNow(a_actorID, a_animate);
				return;
			}
			// Let Sleep In Bed run its dress step (so it forgets the gear it remembered and the next
			// bedtime works as usual), but without animation and without touching any item.
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
			SKSE::log::info("[dress] {:08X} ({}) got up: re-equipping is off, stays as is", a_actorID, NameOf(actor));
			g_keepAsIs = true;
			g_origDressNow(a_actorID, false);
			g_keepAsIs = false;
		}

		bool HookPutOn(RE::Actor* a_actor, RE::FormID a_itemID, std::uint8_t a_slot)
		{
			if (g_keepAsIs) {
				return false;
			}
			return g_origPutOn(a_actor, a_itemID, a_slot);
		}

		bool HookUnequip(RE::ActorEquipManager* a_manager, RE::Actor* a_actor, RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraData, std::uint32_t a_count, const RE::BGSEquipSlot* a_slot, bool a_queueEquip, bool a_forceEquip, bool a_playSounds, bool a_applyNow, const RE::BGSEquipSlot* a_slotToReplace)
		{
			if (g_keepAsIs) {
				return false;
			}
			return g_origUnequip(a_manager, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip, a_playSounds, a_applyNow, a_slotToReplace);
		}

		void HookAssign(RE::Actor* a_actor, RE::TESObjectREFR* a_bed, void* a_slot, const char* a_role)
		{
			if (g_inBedtime && Settings::IsNotFollower(a_actor)) {
				SKSE::log::info("[partner] {:08X} ({}) is a teammate but not a follower: not sent to bed", a_actor->GetFormID(), NameOf(a_actor));
				return;
			}
			if (g_inBedtime && a_bed && a_bed == g_playerBed && g_followers.contains(a_actor) && !g_assignedPartner) {
				g_assignedPartner = a_actor;
			}
			if (g_inBedtime && a_actor && a_bed) {
				g_assignedList.push_back({ a_actor, a_bed });
			}
			g_origAssign(a_actor, a_bed, a_slot, a_role);
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
		                     BytesMatch(base, kRvaAllowedToUse, kBytesAllowedToUse, "AllowedToUse") &
		                     BytesMatch(base, kRvaAssign, kBytesAssign, "Assign") &
		                     BytesMatch(base, kRvaSelectPackage, kBytesSelectPackage, "SelectPackage") &
		                     BytesMatch(base, kRvaDressNow, kBytesDressNow, "DressNow") &
		                     BytesMatch(base, kRvaPutOn, kBytesPutOn, "PutOn") &
		                     BytesMatch(base, kRvaUnequip, kBytesUnequip, "Unequip") &
		                     BytesMatch(base, kRvaCanProcessSleepKey, kBytesCanProcessSleepKey, "CanProcessWithSleepKey") &
		                     BytesMatch(base, kRvaIsSleepKey, kBytesIsSleepKey, "IsSleepKey") &
		                     BytesMatch(base, kRvaSendSleepPackage, kBytesSendSleepPackage, "SendSleepPackage") &
		                     BytesMatch(base, kRvaPartOfOutfit, kBytesPartOfOutfit, "PartOfOutfit");
		if (stamp != kSupportedTimeDateStamp || !bytesOk) {
			g_state = State::kWrongVersion;
			SKSE::log::warn("[patch] SleepInBedSKSE.dll build stamp {} is not Sleep In Bed SKSE 1.4 ({}). Nothing patched.", stamp, kSupportedTimeDateStamp);
			return;
		}

		g_isSleepKey = reinterpret_cast<IsSleepKey_t>(base + kRvaIsSleepKey);
		g_vanillaCanProcess = reinterpret_cast<CanProcess_t*>(base + kRvaVanillaCanProcess);

		const auto init = MH_Initialize();
		if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
			g_state = State::kHookFailed;
			SKSE::log::error("[patch] MinHook could not start");
			return;
		}
		const bool created = Hook(base, kRvaLayFollowersDown, reinterpret_cast<void*>(&HookLayFollowersDown), reinterpret_cast<void**>(&g_origLayFollowersDown), "LayFollowersDown") &&
		                     Hook(base, kRvaSortSleepers, reinterpret_cast<void*>(&HookSortSleepers), reinterpret_cast<void**>(&g_origSortSleepers), "SortSleepers") &&
		                     Hook(base, kRvaAllowedToUse, reinterpret_cast<void*>(&HookAllowedToUse), reinterpret_cast<void**>(&g_origAllowedToUse), "AllowedToUse") &&
		                     Hook(base, kRvaAssign, reinterpret_cast<void*>(&HookAssign), reinterpret_cast<void**>(&g_origAssign), "Assign") &&
		                     Hook(base, kRvaSelectPackage, reinterpret_cast<void*>(&HookSelectPackage), reinterpret_cast<void**>(&g_origSelectPackage), "SelectPackage") &&
		                     Hook(base, kRvaDressNow, reinterpret_cast<void*>(&HookDressNow), reinterpret_cast<void**>(&g_origDressNow), "DressNow") &&
		                     Hook(base, kRvaPutOn, reinterpret_cast<void*>(&HookPutOn), reinterpret_cast<void**>(&g_origPutOn), "PutOn") &&
		                     Hook(base, kRvaUnequip, reinterpret_cast<void*>(&HookUnequip), reinterpret_cast<void**>(&g_origUnequip), "Unequip") &&
		                     Hook(base, kRvaCanProcessSleepKey, reinterpret_cast<void*>(&HookCanProcessSleepKey), reinterpret_cast<void**>(&g_origCanProcessSleepKey), "CanProcessWithSleepKey") &&
		                     Hook(base, kRvaSendSleepPackage, reinterpret_cast<void*>(&HookSendSleepPackage), reinterpret_cast<void**>(&g_origSendSleepPackage), "SendSleepPackage") &&
		                     Hook(base, kRvaPartOfOutfit, reinterpret_cast<void*>(&HookPartOfOutfit), reinterpret_cast<void**>(&g_origPartOfOutfit), "PartOfOutfit");
		if (!created || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
			MH_DisableHook(MH_ALL_HOOKS);
			g_state = State::kHookFailed;
			SKSE::log::error("[patch] hooks could not be enabled. Nothing patched.");
			return;
		}
		g_state = State::kActive;
		std::thread(WatchThread).detach();
		SKSE::log::info("[patch] Sleep In Bed SKSE 1.4 patched (module at {:X})", base);
	}

	State GetState()
	{
		return g_state;
	}

	void SendToBed(RE::Actor* a_actor, RE::TESObjectREFR* a_bed)
	{
		if (g_state == State::kActive && a_actor && a_bed) {
			g_origAssign(a_actor, a_bed, nullptr, kRole);
		}
	}
}
