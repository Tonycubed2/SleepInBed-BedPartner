#include "PCH.h"

#include "Talk.h"

#include "Patch.h"
#include "Settings.h"

// Why a follower gets up when the player talks to her in bed (SkyrimSE.exe 1.6.1170, disassembly):
//
//   TESFurniture "must exit to talk"   +0x268FA0: test dword ptr [furniture+0xE4], 0x8000000 (furnFlags, kMustExitToTalk)
//   its only caller, in the actor's dialogue start (function at +0x660410), call at +0x660582:
//     furniture = base object of the actor's occupied furniture
//     if (MustExitToTalk(furniture) && !actor has ExtraInteraction (0xA9) &&
//         sit/sleep state (actorState1 bits 14-17) == 3 sitting or 7 sleeping)
//         -> the actor leaves the furniture (vfunc 0x6F0) and the conversation is not started now
//
// So only the furniture flag decides it: beds without it (vanilla Orc beds) already let you talk lying down.
// In Skyrim.esm 129 of the 151 sleep furniture forms have the flag.
//
// This file clears that flag on the bed forms followers lie in while the player lies in bed, and puts it
// back when the player gets up. A bed form is shared by every bed of that type, so while the player lies
// in bed, any NPC in a bed of the same type also talks lying down; nothing changes once the player is up.

namespace Talk
{
	namespace
	{
		using Marker = RE::TESFurniture::ActiveMarker;

		// Game main thread only.
		std::unordered_set<RE::FormID> g_cleared;  // TESFurniture forms whose flag this file removed
		std::atomic<bool>              g_tickQueued{ false };

		const char* NameOf(RE::TESForm* a_form)
		{
			const char* name = a_form ? a_form->GetName() : nullptr;
			return name && *name ? name : "?";
		}

		bool Lying(RE::Actor* a_actor)
		{
			return a_actor && a_actor->AsActorState()->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping;
		}

		RE::TESFurniture* BedOf(RE::Actor* a_actor)
		{
			auto* process = a_actor ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
			if (!process) {
				return nullptr;
			}
			auto  refPtr = process->GetOccupiedFurniture().get();
			auto* ref = refPtr.get();
			auto* base = ref ? ref->GetBaseObject() : nullptr;
			return base ? base->As<RE::TESFurniture>() : nullptr;
		}

		void Clear(RE::TESFurniture* a_bed, RE::Actor* a_follower)
		{
			if (!a_bed || !a_bed->furnFlags.any(Marker::kMustExitToTalk)) {
				return;
			}
			a_bed->furnFlags.reset(Marker::kMustExitToTalk);
			g_cleared.insert(a_bed->GetFormID());
			SKSE::log::info("[talk] {:08X} ({}) lies in {:08X} ({}): talking to her no longer makes her get up", a_follower->GetFormID(), NameOf(a_follower), a_bed->GetFormID(), NameOf(a_bed));
		}

		void RestoreAll(const char* a_why)
		{
			if (g_cleared.empty()) {
				return;
			}
			for (const auto id : g_cleared) {
				if (auto* bed = RE::TESForm::LookupByID<RE::TESFurniture>(id)) {
					bed->furnFlags.set(Marker::kMustExitToTalk);
				}
			}
			SKSE::log::info("[talk] {}: \"must exit to talk\" put back on {} bed type(s)", a_why, g_cleared.size());
			g_cleared.clear();
		}

		void Tick()
		{
			g_tickQueued = false;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!Settings::Get().stayInBedToTalk || !Lying(player)) {
				RestoreAll(Settings::Get().stayInBedToTalk ? "the player got up" : "option off");
				return;
			}
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return;
			}
			lists->ForEachHighActor([&](RE::Actor* a_actor) {
				if (a_actor && !a_actor->IsPlayerRef() && !a_actor->IsDead() && a_actor->Is3DLoaded() &&
					a_actor->IsPlayerTeammate() && !Settings::IsNotFollower(a_actor) && Lying(a_actor)) {
					Clear(BedOf(a_actor), a_actor);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		void Thread()
		{
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				if (g_tickQueued.exchange(true)) {
					continue;
				}
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([] { Tick(); });
				} else {
					g_tickQueued = false;
				}
			}
		}
	}

	void Install()
	{
		if (Patch::GetState() != Patch::State::kActive) {
			return;
		}
		std::thread(Thread).detach();
		SKSE::log::info("[talk] watching beds (followers stay in bed when you talk to them: {})", Settings::Get().stayInBedToTalk);
	}

	void OnLoadingGame()
	{
		RestoreAll("loading a save");
	}
}
