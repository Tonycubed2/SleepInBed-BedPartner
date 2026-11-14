#include "PCH.h"

#include "Weapons.h"

#include "Patch.h"
#include "Settings.h"

// windows.h (pulled in through the PCH) renames GetObject to GetObjectW.
#undef GetObject

// Sleep In Bed SKSE 1.4 (Undress.cpp): with the "stay dressed" choice (mode Off) UndressNow and
// BeginBeforeBed return at once, so nothing is taken off, weapons included. With any undress or
// sleepwear choice it always takes weapons and torches off itself (WornEquipment), one frame after
// the actor starts lying down. So anything still held once an actor has lain in bed for a moment
// was left on by "stay dressed": this file takes it off and puts it back when the actor gets up.
// Same equip calls and hand slots as Sleep In Bed's own TakeOff / PutOn.

namespace Weapons
{
	namespace
	{
		constexpr auto kSettleTime = std::chrono::milliseconds(1500);  // Sleep In Bed undresses one frame after lying down

		enum Hand : std::uint8_t
		{
			kRight = 0,
			kLeft = 1,
			kShield = 2,
		};

		struct Taken
		{
			RE::FormID   form;
			std::uint8_t hand;
		};

		// Game main thread only.
		std::unordered_map<RE::FormID, std::vector<Taken>>                           g_taken;
		std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point>        g_lyingSince;
		std::unordered_set<RE::FormID>                                               g_checked;  // this bedtime
		std::atomic<bool>                                                            g_tickQueued{ false };

		const char* NameOf(RE::Actor* a_actor)
		{
			const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
			return name && *name ? name : "?";
		}

		const RE::BGSEquipSlot* LeftHandSlot()
		{
			auto* manager = RE::BGSDefaultObjectManager::GetSingleton();
			return manager ? manager->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECTS::kLeftHandEquip) : nullptr;
		}

		bool IsShield(RE::TESBoundObject& a_object)
		{
			auto* armor = a_object.As<RE::TESObjectARMO>();
			return armor && armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kShield);
		}

		RE::TESObjectREFR::InventoryItemMap HandItems(RE::Actor* a_actor)
		{
			return a_actor->GetInventory([](RE::TESBoundObject& a_object) {
				return a_object.IsWeapon() || a_object.Is(RE::FormType::Light) || IsShield(a_object);
			});
		}

		std::vector<Taken> Held(RE::Actor* a_actor)
		{
			std::vector<Taken> held;
			for (const auto& [object, data] : HandItems(a_actor)) {
				const auto& [count, entry] = data;
				if (!object || count <= 0 || !entry) {
					continue;
				}
				if (object->IsWeapon()) {
					if (entry->IsWorn(false)) {
						held.push_back({ object->GetFormID(), kRight });
					}
					if (entry->IsWorn(true)) {
						held.push_back({ object->GetFormID(), kLeft });
					}
				} else if (entry->IsWorn()) {
					held.push_back({ object->GetFormID(), object->Is(RE::FormType::Light) ? kLeft : kShield });
				}
			}
			// Right hand first, so a pair of the same one-handed weapon goes back into both hands.
			std::sort(held.begin(), held.end(), [](const Taken& a, const Taken& b) { return a.hand < b.hand; });
			return held;
		}

		RE::TESBoundObject* Carried(RE::Actor* a_actor, RE::FormID a_form)
		{
			for (const auto& [object, data] : HandItems(a_actor)) {
				if (object && object->GetFormID() == a_form && data.first > 0) {
					return object;
				}
			}
			return nullptr;
		}

		bool Watched(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsDead() || a_actor->IsChild() || !a_actor->Is3DLoaded()) {
				return false;
			}
			return a_actor->IsPlayerRef() || (a_actor->IsPlayerTeammate() && !Settings::IsNotFollower(a_actor));
		}

		void TakeOff(RE::Actor* a_actor)
		{
			auto* manager = RE::ActorEquipManager::GetSingleton();
			if (!manager || a_actor->IsInCombat()) {
				return;
			}
			auto held = Held(a_actor);
			if (held.empty()) {
				return;
			}
			const auto* left = LeftHandSlot();
			std::vector<Taken> done;
			for (const auto& item : held) {
				auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(item.form);
				if (!object) {
					continue;
				}
				const auto* slot = item.hand == kLeft ? left : nullptr;
				if (manager->UnequipObject(a_actor, object, nullptr, 1, slot, true, false, false, false)) {
					done.push_back(item);
				}
			}
			if (!done.empty()) {
				auto& list = g_taken[a_actor->GetFormID()];
				list.insert(list.end(), done.begin(), done.end());
				SKSE::log::info("[weapons] {:08X} ({}) lies in bed dressed: {} weapon/shield/torch item(s) taken off", a_actor->GetFormID(), NameOf(a_actor), done.size());
			}
		}

		bool HandFree(RE::Actor* a_actor, std::uint8_t a_hand)
		{
			switch (a_hand) {
			case kRight:
				return a_actor->GetEquippedObject(false) == nullptr;
			case kLeft:
				return a_actor->GetEquippedObject(true) == nullptr;
			default:
				return a_actor->GetEquippedObject(true) == nullptr && a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kShield) == nullptr;
			}
		}

		void PutBack(RE::Actor* a_actor, const std::vector<Taken>& a_items)
		{
			const bool redress = a_actor->IsPlayerRef() ? Settings::Get().redressPlayer : Settings::Get().redressFollowers;
			if (!redress) {
				SKSE::log::info("[weapons] {:08X} ({}) got up: re-equipping is off, weapons stay off", a_actor->GetFormID(), NameOf(a_actor));
				return;
			}
			auto* manager = RE::ActorEquipManager::GetSingleton();
			if (!manager) {
				return;
			}
			const auto* left = LeftHandSlot();
			std::uint32_t count = 0;
			for (const auto& item : a_items) {
				auto* object = Carried(a_actor, item.form);
				if (!object || !HandFree(a_actor, item.hand)) {
					continue;  // dropped, sold, or that hand already holds something else
				}
				const auto* slot = item.hand == kLeft ? left : nullptr;
				manager->EquipObject(a_actor, object, nullptr, 1, slot, false, false, false, false);
				++count;
			}
			SKSE::log::info("[weapons] {:08X} ({}) got up: {} of {} item(s) put back on", a_actor->GetFormID(), NameOf(a_actor), count, a_items.size());
		}

		void Check(RE::Actor* a_actor, std::chrono::steady_clock::time_point a_now)
		{
			const auto id = a_actor->GetFormID();
			const auto state = a_actor->AsActorState()->GetSitSleepState();
			if (state == RE::SIT_SLEEP_STATE::kIsSleeping) {
				if (!Settings::Get().unequipWeaponsDressed || g_checked.contains(id)) {
					return;
				}
				auto [it, fresh] = g_lyingSince.try_emplace(id, a_now);
				if (fresh || a_now - it->second < kSettleTime) {
					return;
				}
				g_checked.insert(id);
				TakeOff(a_actor);
			} else if (state < RE::SIT_SLEEP_STATE::kWantToSleep) {
				g_lyingSince.erase(id);
				g_checked.erase(id);
			}
		}

		void Tick()
		{
			g_tickQueued = false;
			const auto now = std::chrono::steady_clock::now();

			if (auto* player = RE::PlayerCharacter::GetSingleton(); Watched(player)) {
				Check(player, now);
			}
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				lists->ForEachHighActor([&](RE::Actor* a_actor) {
					if (a_actor && !a_actor->IsPlayerRef() && Watched(a_actor)) {
						Check(a_actor, now);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}

			for (auto it = g_taken.begin(); it != g_taken.end();) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
				if (!actor || actor->IsDead()) {
					it = g_taken.erase(it);
					continue;
				}
				if (!actor->Is3DLoaded() || actor->AsActorState()->GetSitSleepState() >= RE::SIT_SLEEP_STATE::kWantToSleep) {
					++it;
					continue;
				}
				const auto items = std::move(it->second);
				it = g_taken.erase(it);
				PutBack(actor, items);
			}
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
		SKSE::log::info("[weapons] watching beds (take weapons off when sleeping dressed: {})", Settings::Get().unequipWeaponsDressed);
	}

	void OnLoadingGame()
	{
		// SKSE messages arrive on the game's main thread, like every use of these maps.
		g_taken.clear();
		g_lyingSince.clear();
		g_checked.clear();
	}
}
