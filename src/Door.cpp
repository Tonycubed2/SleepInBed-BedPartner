#include "PCH.h"

#include "Door.h"

#include "Patch.h"
#include "Settings.h"

// "Clear the room and close the door" (like the old Go To Bed addon):
// when the player lies down in an interior, the nearest ordinary door to the bed (not a load door) is taken
// as the room door. NPCs on the bed's side of it (not followers) walk out; a follower walks to the door,
// closes it, then goes to bed. While the player stays in bed, if the door is opened again, she gets up and
// closes it again. Everything is released when the player gets up.
//
// "Bed's side of the door": the door panel is the thin axis of the door's bounds; the side is the sign of
// the distance to that plane, compared with the bed's.

namespace Door
{
	bool OnDuty(RE::Actor* a_actor);

	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr float         kDoorSearchRadius = 1000.0f;  // door must be this close to the bed
		constexpr float         kRoomRadius = 1100.0f;        // NPCs this close to the bed can be "in the room"
		constexpr float         kSameFloor = 120.0f;          // height difference still counted as the same floor (a storey is ~256)
		constexpr float         kOutsideDistance = 220.0f;    // where leavers go, beyond the door
		constexpr float         kInsideDistance = 90.0f;      // where the closer stands, in front of the door
		constexpr float         kAtDoor = 170.0f;             // close enough to close it
		constexpr float         kLeaveTimeout = 25.0f;
		constexpr float         kDoorTimeout = 20.0f;
		constexpr int           kMaxCloses = 10;
		constexpr RE::FormID    kXMarker = 0x3B;
		constexpr std::uint32_t kLeaveRadius = 64;
		constexpr std::uint32_t kDoorRadius = 48;
		constexpr auto          kRole = "follower";

		enum class Phase
		{
			kIdle,
			kWaitLeavers,
			kGoingToDoor,
			kClosing,
			kGuard,
		};

		struct State
		{
			Phase                        phase{ Phase::kIdle };
			Clock::time_point            phaseStart{};
			RE::ObjectRefHandle          door;
			RE::ObjectRefHandle          playerBed;
			RE::NiPoint3                 doorPos;
			RE::NiPoint3                 normal;   // unit, horizontal
			float                        bedSide{ 1.0f };
			RE::NiPoint3                 bedPos;
			RE::ActorHandle              closer;
			RE::ObjectRefHandle          closerBed;
			std::vector<RE::ActorHandle> leavers;
			std::unordered_set<RE::FormID> excluded;  // player, followers, spouse
			RE::ObjectRefHandle          outsideMarker;
			RE::ObjectRefHandle          insideMarker;
			int                          closes{ 0 };
		};

		State g_state;  // main thread only

		// Read by the package-selection guard on AI threads.
		std::mutex                     g_lock;
		std::unordered_set<RE::FormID> g_managed;   // leavers + closer on duty: their travel package is kept
		RE::FormID                     g_onDuty{ 0 };

		std::atomic<bool> g_tickQueued{ false };

		const char* NameOf(RE::TESObjectREFR* a_ref)
		{
			const char* name = a_ref ? a_ref->GetDisplayFullName() : nullptr;
			return name && *name ? name : "?";
		}

		float Seconds(Clock::time_point a_since)
		{
			return std::chrono::duration<float>(Clock::now() - a_since).count();
		}

		bool PlayerLyingInBed()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player && player->AsActorState()->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping;
		}

		void SetPhase(Phase a_phase)
		{
			g_state.phase = a_phase;
			g_state.phaseStart = Clock::now();
		}

		float SideOf(const RE::NiPoint3& a_pos)
		{
			const auto d = a_pos - g_state.doorPos;
			return d.x * g_state.normal.x + d.y * g_state.normal.y;
		}

		bool InRoom(RE::TESObjectREFR* a_ref)
		{
			const auto pos = a_ref->GetPosition();
			if (std::abs(pos.z - g_state.bedPos.z) > kSameFloor || pos.GetDistance(g_state.bedPos) > kRoomRadius) {
				return false;
			}
			const float side = SideOf(pos);
			return side * g_state.bedSide > 0.0f;
		}

		bool IsOrdinaryDoor(RE::TESObjectREFR* a_ref)
		{
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			return base && base->Is(RE::FormType::Door) && !a_ref->IsDisabled() && !a_ref->IsDeleted() && !a_ref->extraList.HasType(RE::ExtraDataType::kTeleport);
		}

		bool DoorOpen(RE::TESObjectREFR* a_door)
		{
			const auto state = RE::BGSOpenCloseForm::GetOpenState(a_door);
			return state == RE::BGSOpenCloseForm::OPEN_STATE::kOpen || state == RE::BGSOpenCloseForm::OPEN_STATE::kOpening;
		}

		RE::NiPointer<RE::TESObjectREFR> PlaceMarker(RE::TESObjectREFR* a_at, const RE::NiPoint3& a_pos)
		{
			auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
			if (!base || !a_at) {
				return nullptr;
			}
			auto marker = a_at->PlaceObjectAtMe(base, false);
			if (marker) {
				marker->SetPosition(a_pos);
			}
			return marker;
		}

		void RemoveMarker(RE::ObjectRefHandle& a_handle)
		{
			if (auto marker = a_handle.get(); marker) {
				marker->Disable();
				marker->SetDelete(true);
			}
			a_handle.reset();
		}

		bool SendTravel(RE::Actor* a_actor, RE::TESObjectREFR* a_target, std::uint32_t a_radius)
		{
			auto* process = a_actor && a_target ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
			if (!process) {
				return false;
			}
			auto* package = RE::TESPackage::CreatePackage(RE::PACKAGE_TYPE::kTravel);
			if (!package) {
				return false;
			}
			if (!package->packLoc) {
				// Same as Sleep In Bed: created packages come without a location object.
				auto* location = RE::malloc<RE::PackageLocation>(sizeof(RE::PackageLocation));
				if (!location) {
					return false;
				}
				std::memset(static_cast<void*>(location), 0, sizeof(RE::PackageLocation));
				REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PackageLocation[0] };
				*reinterpret_cast<std::uintptr_t*>(location) = vtable.address();
				package->packLoc = location;
			}
			auto* location = package->packLoc;
			package->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kMustComplete);
			location->locType = RE::PackageLocation::Type::kNearReference;
			location->rad = a_radius;
			location->data.refHandle = a_target->GetHandle();
			a_actor->PutCreatedPackage(package, false, true, false);
			const bool current = process->currentPackage.package == package;
			SKSE::log::info("[room] {:08X} ({}) travel package to {:08X}: current={} procedure={}", a_actor->GetFormID(), NameOf(a_actor), a_target->GetFormID(), current, package->procedureType.underlying());
			return current;
		}

		void ReleaseTravel(RE::Actor* a_actor)
		{
			auto* process = a_actor ? a_actor->GetActorRuntimeData().currentProcess : nullptr;
			auto* package = process ? process->currentPackage.package : nullptr;
			if (package && package->packData.packType == RE::PACKAGE_TYPE::kTravel && package->packData.packFlags.any(RE::PACKAGE_DATA::GeneralFlag::kCreated)) {
				package->packData.packFlags.reset(RE::PACKAGE_DATA::GeneralFlag::kMustComplete);
				a_actor->EvaluatePackage(true, false);
			}
		}

		void Manage(RE::Actor* a_actor, bool a_on)
		{
			std::scoped_lock lock(g_lock);
			if (a_on) {
				g_managed.insert(a_actor->GetFormID());
			} else {
				g_managed.erase(a_actor->GetFormID());
			}
		}

		void SetDuty(RE::Actor* a_actor)
		{
			std::scoped_lock lock(g_lock);
			g_onDuty = a_actor ? a_actor->GetFormID() : 0;
		}

		void SendLeaversOut(RE::TESObjectREFR* a_bed)
		{
			auto  markerPtr = g_state.outsideMarker.get();
			auto* marker = markerPtr.get();
			auto* tes = RE::TES::GetSingleton();
			if (!marker || !tes) {
				return;
			}
			tes->ForEachReferenceInRange(a_bed, kRoomRadius, [&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (!actor || actor->IsPlayerRef() || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded() ||
					(actor->IsPlayerTeammate() && !Settings::IsNotFollower(actor)) || actor->IsInCombat() || g_state.excluded.contains(actor->GetFormID()) || !InRoom(actor)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				for (auto& handle : g_state.leavers) {
					if (handle.get().get() == actor) {
						return RE::BSContainer::ForEachResult::kContinue;
					}
				}
				Manage(actor, true);
				if (SendTravel(actor, marker, kLeaveRadius)) {
					g_state.leavers.push_back(actor->GetHandle());
					SKSE::log::info("[room] {:08X} ({}) is in the room: sent out", actor->GetFormID(), NameOf(actor));
				} else {
					Manage(actor, false);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		bool LeaversOut()
		{
			for (auto& handle : g_state.leavers) {
				auto  ptr = handle.get();
				auto* actor = ptr.get();
				if (actor && !actor->IsDead() && actor->Is3DLoaded() && InRoom(actor)) {
					return false;
				}
			}
			return true;
		}

		const char* OpenStateName(RE::TESObjectREFR* a_door)
		{
			switch (RE::BGSOpenCloseForm::GetOpenState(a_door)) {
			case RE::BGSOpenCloseForm::OPEN_STATE::kOpen:
				return "open";
			case RE::BGSOpenCloseForm::OPEN_STATE::kOpening:
				return "opening";
			case RE::BGSOpenCloseForm::OPEN_STATE::kClosed:
				return "closed";
			case RE::BGSOpenCloseForm::OPEN_STATE::kClosing:
				return "closing";
			default:
				return "none";
			}
		}

		// The closer uses the door like anyone opening or closing it (door animation, sound).
		void ActivateDoor(RE::TESObjectREFR* a_door, RE::Actor* a_by)
		{
			++g_state.closes;
			if (a_by) {
				a_door->ActivateRef(a_by, 0, nullptr, 1, false);
				SKSE::log::info("[room] {:08X} ({}) closes door {:08X} ({} -> {}) ({}/{})", a_by->GetFormID(), NameOf(a_by), a_door->GetFormID(), "open", OpenStateName(a_door), g_state.closes, kMaxCloses);
			} else {
				RE::BGSOpenCloseForm::SetOpenState(a_door, false, true);
				SKSE::log::info("[room] door {:08X} closed by the patch: now {} ({}/{})", a_door->GetFormID(), OpenStateName(a_door), g_state.closes, kMaxCloses);
			}
		}

		// Called a moment after ActivateDoor: if the door is still open, it is shut directly.
		void MakeSureClosed(RE::TESObjectREFR* a_door)
		{
			if (DoorOpen(a_door)) {
				RE::BGSOpenCloseForm::SetOpenState(a_door, false, true);
				SKSE::log::info("[room] door {:08X} still open after being used: shut directly, now {}", a_door->GetFormID(), OpenStateName(a_door));
			} else {
				SKSE::log::info("[room] door {:08X} is {}", a_door->GetFormID(), OpenStateName(a_door));
			}
		}

		void FinishDuty(RE::Actor* a_closer)
		{
			SetDuty(nullptr);
			if (!a_closer) {
				return;
			}
			Manage(a_closer, false);
			auto  bedPtr = g_state.closerBed.get();
			auto* bed = bedPtr.get();
			if (bed && PlayerLyingInBed()) {
				SKSE::log::info("[room] {:08X} ({}) goes (back) to bed {:08X}", a_closer->GetFormID(), NameOf(a_closer), bed->GetFormID());
				Patch::SendToBed(a_closer, bed);
			} else {
				ReleaseTravel(a_closer);
			}
		}

		void StartClosing(RE::TESObjectREFR* a_door)
		{
			auto  closerPtr = g_state.closer.get();
			auto* closer = closerPtr.get();
			auto  markerPtr = g_state.insideMarker.get();
			auto* marker = markerPtr.get();
			if (closer && marker && !closer->IsDead() && closer->Is3DLoaded()) {
				SetDuty(closer);
				Manage(closer, true);
				if (SendTravel(closer, marker, kDoorRadius)) {
					SKSE::log::info("[room] {:08X} ({}) goes to close door {:08X}", closer->GetFormID(), NameOf(closer), a_door->GetFormID());
					SetPhase(Phase::kGoingToDoor);
					return;
				}
				Manage(closer, false);
				SetDuty(nullptr);
			}
			ActivateDoor(a_door, nullptr);
			SetPhase(Phase::kClosing);
		}

		void Stop(const char* a_why)
		{
			if (g_state.phase == Phase::kIdle) {
				return;
			}
			SKSE::log::info("[room] done: {}", a_why);
			std::vector<RE::FormID> managed;
			{
				std::scoped_lock lock(g_lock);
				managed.assign(g_managed.begin(), g_managed.end());
				g_managed.clear();
				g_onDuty = 0;
			}
			for (auto id : managed) {
				ReleaseTravel(RE::TESForm::LookupByID<RE::Actor>(id));
			}
			RemoveMarker(g_state.outsideMarker);
			RemoveMarker(g_state.insideMarker);
			g_state = State{};
		}

		void Tick()
		{
			g_tickQueued = false;
			if (g_state.phase == Phase::kIdle) {
				return;
			}
			if (!PlayerLyingInBed()) {
				Stop("the player got up");
				return;
			}
			auto  doorPtr = g_state.door.get();
			auto* door = doorPtr.get();
			auto  bedPtr = g_state.playerBed.get();
			auto* bed = bedPtr.get();
			if (!door || !bed) {
				Stop("door or bed gone");
				return;
			}
			switch (g_state.phase) {
			case Phase::kWaitLeavers:
				if (LeaversOut() || Seconds(g_state.phaseStart) > kLeaveTimeout) {
					SKSE::log::info("[room] room is clear{}", LeaversOut() ? "" : " (time is up)");
					if (DoorOpen(door) && g_state.closes < kMaxCloses) {
						StartClosing(door);
					} else {
						SetPhase(Phase::kGuard);
					}
				}
				break;
			case Phase::kGoingToDoor:
				{
					auto  closerPtr = g_state.closer.get();
					auto* closer = closerPtr.get();
					const bool arrived = closer && closer->GetPosition().GetDistance(g_state.doorPos) < kAtDoor;
					if (!closer || closer->IsDead() || arrived || Seconds(g_state.phaseStart) > kDoorTimeout) {
						if (closer && !arrived) {
							SKSE::log::info("[room] {:08X} ({}) did not reach the door in {:.0f} s; closing it anyway", closer->GetFormID(), NameOf(closer), kDoorTimeout);
						}
						if (DoorOpen(door)) {
							ActivateDoor(door, closer);
						}
						SetPhase(Phase::kClosing);
					}
				}
				break;
			case Phase::kClosing:
				if (Seconds(g_state.phaseStart) > 1.5f) {
					MakeSureClosed(door);
					auto closerPtr = g_state.closer.get();
					if (OnDuty(closerPtr.get())) {
						FinishDuty(closerPtr.get());
					}
					SetPhase(Phase::kGuard);
				}
				break;
			case Phase::kGuard:
				if (Seconds(g_state.phaseStart) > 2.0f && DoorOpen(door) && g_state.closes < kMaxCloses) {
					SKSE::log::info("[room] door {:08X} was opened again", door->GetFormID());
					SendLeaversOut(bed);
					if (!LeaversOut()) {
						SetPhase(Phase::kWaitLeavers);
					} else {
						StartClosing(door);
					}
				}
				break;
			default:
				break;
			}
		}

		void Thread()
		{
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				if (g_state.phase == Phase::kIdle || g_tickQueued.exchange(true)) {
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

	void Begin(RE::TESObjectREFR* a_playerBed, RE::Actor* a_closer, RE::TESObjectREFR* a_closerBed, const std::vector<Assigned>& a_assigned)
	{
		Stop("a new bedtime started");
		if (!Settings::Get().clearRoom || !a_playerBed) {
			return;
		}
		auto* cell = a_playerBed->GetParentCell();
		auto* tes = RE::TES::GetSingleton();
		if (!cell || !cell->IsInteriorCell() || !tes) {
			return;
		}
		const auto           bedPos = a_playerBed->GetPosition();
		RE::TESObjectREFR*   door = nullptr;
		float                best = kDoorSearchRadius;
		tes->ForEachReferenceInRange(a_playerBed, kDoorSearchRadius + 1000.0f, [&](RE::TESObjectREFR* a_ref) {
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base || !base->Is(RE::FormType::Door) || a_ref->GetParentCell() != cell) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			const auto  pos = a_ref->GetPosition();
			const float distance = pos.GetDistance(bedPos);
			const float dz = pos.z - bedPos.z;
			const bool  ordinary = IsOrdinaryDoor(a_ref);
			SKSE::log::info("[room] door candidate {:08X} (base {:08X}): {:.0f} units, height {:+.0f}, {}, {}", a_ref->GetFormID(), base->GetFormID(), distance, dz, ordinary ? "room door" : "load door/disabled", OpenStateName(a_ref));
			if (ordinary && std::abs(dz) <= kSameFloor && distance < best) {
				best = distance;
				door = a_ref;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (!door) {
			SKSE::log::info("[room] no room door near bed {:08X}", a_playerBed->GetFormID());
			return;
		}

		// The door panel's normal is the thin axis of its bounds, turned by the door's heading.
		auto*       base = door->GetBaseObject();
		const auto& bounds = base->boundData;
		const float sizeX = static_cast<float>(bounds.boundMax.x - bounds.boundMin.x);
		const float sizeY = static_cast<float>(bounds.boundMax.y - bounds.boundMin.y);
		const float heading = door->GetAngleZ();
		RE::NiPoint3 normal = sizeY <= sizeX ? RE::NiPoint3{ std::sin(heading), std::cos(heading), 0.0f } : RE::NiPoint3{ std::cos(heading), -std::sin(heading), 0.0f };

		g_state = State{};
		g_state.door = door->GetHandle();
		g_state.playerBed = a_playerBed->GetHandle();
		g_state.doorPos = door->GetPosition();
		g_state.normal = normal;
		g_state.bedPos = bedPos;
		const float bedSide = SideOf(bedPos);
		g_state.bedSide = bedSide >= 0.0f ? 1.0f : -1.0f;
		g_state.excluded.insert(0x14);
		for (const auto& entry : a_assigned) {
			if (entry.actor) {
				g_state.excluded.insert(entry.actor->GetFormID());
			}
		}
		const auto outside = g_state.doorPos - normal * (g_state.bedSide * kOutsideDistance);
		const auto inside = g_state.doorPos + normal * (g_state.bedSide * kInsideDistance);
		if (auto marker = PlaceMarker(door, outside)) {
			g_state.outsideMarker = marker->GetHandle();
		}
		if (auto marker = PlaceMarker(door, inside)) {
			g_state.insideMarker = marker->GetHandle();
		}
		if (a_closer && a_closerBed) {
			g_state.closer = a_closer->GetHandle();
			g_state.closerBed = a_closerBed->GetHandle();
		}
		SKSE::log::info("[room] bed {:08X}: room door {:08X} ({}) {:.0f} units away, {}; bed side {:+.0f}; closer: {}", a_playerBed->GetFormID(), door->GetFormID(), NameOf(door), best, DoorOpen(door) ? "open" : "closed", bedSide, a_closer ? NameOf(a_closer) : "none");

		SetPhase(Phase::kGuard);
		SendLeaversOut(a_playerBed);
		if (!g_state.leavers.empty()) {
			SetPhase(Phase::kWaitLeavers);
		} else if (DoorOpen(door)) {
			StartClosing(door);
		}
	}

	bool OnDuty(RE::Actor* a_actor)
	{
		std::scoped_lock lock(g_lock);
		return a_actor && g_onDuty != 0 && a_actor->GetFormID() == g_onDuty;
	}

	bool KeepsPackage(RE::Actor* a_actor, RE::TESPackage* a_package)
	{
		if (!a_actor || !a_package || a_package->packData.packType != RE::PACKAGE_TYPE::kTravel || !a_package->packData.packFlags.any(RE::PACKAGE_DATA::GeneralFlag::kCreated)) {
			return false;
		}
		{
			std::scoped_lock lock(g_lock);
			if (!g_managed.contains(a_actor->GetFormID())) {
				return false;
			}
		}
		return PlayerLyingInBed();
	}

	void Install()
	{
		std::thread(Thread).detach();
	}
}
