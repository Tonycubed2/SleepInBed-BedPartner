#include "PCH.h"

#include "Spoon.h"

#include "Patch.h"
#include "Settings.h"

#include <numbers>

#include <Windows.h>

// windows.h renames GetObject to GetObjectW.
#undef GetObject

// Double Bed Spooning - OAR (CommonDominator, Nexus 118088) replaces the double-bed sleep animations
// (bed_leftsleep / bed_rightsleep) with 3 spooning poses, each chosen by an independent random roll, so the two
// sleepers often get different poses and someone alone in a double bed gets one too. The Double Bed Spooning
// patch replaces its 3 OAR configs: a pose only plays for actors with that pose's marker ability
// (OAR condition HasSpell, abilities in SleepInBedPartner - Spooning.esp), with replaceOnLoop on so the player,
// who lies down before the partner is chosen, switches at the next loop. Here one pose is picked per bedtime
// and given to the player and the bed partner; both lose it when the player gets up.
//
// Spooning of Skyrim (Nexus 153263) puts a companion in the player's single bed or bedroll by placing a hidden
// bed spot (its furniture HSFBed / HSFBedroll...) next to the player (BedQuestScript.PlaceOneBed: offsets and
// bed type from its globals for spooner A). With "Share single beds and bedrolls" on, the same spot is placed
// the same way and the bed partner is sent there through Sleep In Bed. Skipped while Spooning of Skyrim's own
// spooner slots are in use, so the two never send the same follower.

namespace Spoon
{
	namespace
	{
		constexpr auto kSpoonPlugin = "SleepInBedPartner - Spooning.esp"sv;
		constexpr auto kSoSPlugin = "Spooning Of Skyrim.esp"sv;
		constexpr std::array<RE::FormID, 3> kPoseSpells{ 0x800, 0x801, 0x802 };  // Position 3, 4, 6
		constexpr std::array<int, 3>        kPoseNumbers{ 3, 4, 6 };

		// Game main thread only.
		RE::SpellItem*               g_pose = nullptr;  // pose of this bedtime
		std::vector<RE::ActorHandle> g_tagged;
		RE::ObjectRefHandle          g_spot;            // placed Spooning of Skyrim spot
		bool                         g_sweep = true;    // after a load: remove leftovers once the player is up
		std::atomic<bool>            g_tickQueued{ false };

		const char* NameOf(RE::Actor* a_actor)
		{
			const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
			return name && *name ? name : "?";
		}

		template <class T>
		T* Lookup(RE::FormID a_id, std::string_view a_plugin)
		{
			auto* data = RE::TESDataHandler::GetSingleton();
			return data ? data->LookupForm<T>(a_id, a_plugin) : nullptr;
		}

		RE::SpellItem* PoseSpell(std::size_t a_index)
		{
			return Lookup<RE::SpellItem>(kPoseSpells[a_index], kSpoonPlugin);
		}

		bool FileHas(const std::filesystem::path& a_path, std::string_view a_text)
		{
			std::ifstream file(a_path, std::ios::binary);
			if (!file) {
				return false;
			}
			const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			return content.find(a_text) != std::string::npos;
		}

		const std::filesystem::path kDbsFolder = L"Data\\meshes\\actors\\character\\animations\\OpenAnimationReplacer\\Double Bed Spooning - OAR";

		bool Lying(RE::Actor* a_actor)
		{
			return a_actor && a_actor->AsActorState()->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping;
		}

		bool Up(RE::Actor* a_actor)
		{
			return !a_actor || a_actor->AsActorState()->GetSitSleepState() < RE::SIT_SLEEP_STATE::kWantToSleep;
		}

		void Untag(const char* a_why)
		{
			if (g_pose && !g_tagged.empty()) {
				for (auto& handle : g_tagged) {
					auto  ptr = handle.get();
					auto* actor = ptr.get();
					if (actor) {
						actor->RemoveSpell(g_pose);
					}
				}
				SKSE::log::info("[spoon] {}: spooning pose removed from {} actor(s)", a_why, g_tagged.size());
			}
			g_tagged.clear();
			g_pose = nullptr;
		}

		void RemoveSpot(const char* a_why)
		{
			auto  ptr = g_spot.get();
			auto* spot = ptr.get();
			if (spot) {
				spot->Disable();
				spot->SetDelete(true);
				SKSE::log::info("[spoon] {}: Spooning of Skyrim bed spot removed", a_why);
			}
			g_spot = {};
		}

		// After a load the actors who had a pose (saved with the game) are unknown: remove the pose abilities
		// from the player and everyone loaded.
		void Sweep()
		{
			g_sweep = false;
			std::array<RE::SpellItem*, 3> spells{ PoseSpell(0), PoseSpell(1), PoseSpell(2) };
			auto strip = [&](RE::Actor* a_actor) {
				for (auto* spell : spells) {
					if (spell && a_actor && a_actor->HasSpell(spell)) {
						a_actor->RemoveSpell(spell);
						SKSE::log::info("[spoon] after loading: {:08X} ({}) loses a leftover spooning pose", a_actor->GetFormID(), NameOf(a_actor));
					}
				}
			};
			strip(RE::PlayerCharacter::GetSingleton());
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				lists->ForEachHighActor([&](RE::Actor* a_actor) {
					strip(a_actor);
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		}

		void Tick()
		{
			g_tickQueued = false;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !Up(player)) {
				return;
			}
			if (g_sweep && player->Is3DLoaded()) {
				Sweep();
			}
			if (g_pose) {
				Untag("the player got up");
			}
			RemoveSpot("the player got up");
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

		std::mt19937& Random()
		{
			static std::mt19937 engine{ std::random_device{}() };
			return engine;
		}

		float GlobalValue(RE::FormID a_id, float a_default)
		{
			auto* global = Lookup<RE::TESGlobal>(a_id, kSoSPlugin);
			return global ? global->value : a_default;
		}
	}

	const char* DoubleBedMissing()
	{
		if (!PoseSpell(0) || !PoseSpell(1) || !PoseSpell(2)) {
			return "Needs the Double Bed Spooning patch (SleepInBedPartner - Spooning.esp is not active).";
		}
		if (!GetModuleHandleW(L"OpenAnimationReplacer.dll")) {
			return "Needs Open Animation Replacer.";
		}
		std::error_code ec;
		if (!std::filesystem::exists(kDbsFolder / L"Position 3" / L"bed_leftsleep.hkx", ec)) {
			return "Needs Double Bed Spooning - OAR (Nexus 118088).";
		}
		if (!FileHas(kDbsFolder / L"Position 3" / L"config.json", kSpoonPlugin)) {
			return "The Double Bed Spooning patch must load after Double Bed Spooning - OAR (its config files are overwritten).";
		}
		return "";
	}

	bool DoubleBedAvailable() { return *DoubleBedMissing() == '\0'; }

	const char* SingleBedMissing()
	{
		if (!Lookup<RE::TESFurniture>(0xD71, kSoSPlugin)) {
			return "Needs Spooning of Skyrim (Nexus 153263).";
		}
		return "";
	}

	bool SingleBedAvailable() { return *SingleBedMissing() == '\0'; }

	void PairInDoubleBed(RE::Actor* a_partner)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!a_partner || !player || !Settings::Get().spoonDoubleBeds || !DoubleBedAvailable()) {
			return;
		}
		Untag("a new bedtime started");
		std::vector<std::size_t> allowed;
		for (std::size_t i = 0; i < kPoseSpells.size(); ++i) {
			if ((Settings::Get().spoonPoses & (1u << i)) != 0) {
				allowed.push_back(i);
			}
		}
		if (allowed.empty()) {
			SKSE::log::info("[spoon] no spooning pose ticked: normal sleep animations");
			return;
		}
		const auto index = allowed[std::uniform_int_distribution<std::size_t>(0, allowed.size() - 1)(Random())];
		g_pose = PoseSpell(index);
		if (!g_pose) {
			return;
		}
		player->AddSpell(g_pose);
		a_partner->AddSpell(g_pose);
		g_tagged = { player->GetHandle(), a_partner->GetHandle() };
		SKSE::log::info("[spoon] the player and {:08X} ({}) share the bed: spooning pose {}", a_partner->GetFormID(), NameOf(a_partner), kPoseNumbers[index]);
	}

	void PartnerChanged(RE::Actor* a_old, RE::Actor* a_new)
	{
		if (!g_pose) {
			return;
		}
		if (a_old) {
			a_old->RemoveSpell(g_pose);
			std::erase_if(g_tagged, [&](const RE::ActorHandle& h) { return h.get().get() == a_old; });
		}
		if (a_new) {
			a_new->AddSpell(g_pose);
			g_tagged.push_back(a_new->GetHandle());
			SKSE::log::info("[spoon] spooning pose goes to the new bed partner {:08X} ({})", a_new->GetFormID(), NameOf(a_new));
		}
	}

	RE::TESObjectREFR* PlaceSingleBedSpot(RE::TESObjectREFR* a_playerBed)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!a_playerBed || !player || !Settings::Get().shareSingleBeds || !SingleBedAvailable()) {
			return nullptr;
		}
		auto* base = a_playerBed->GetBaseObject();
		auto* bedList = Lookup<RE::BGSListForm>(0xD6F, kSoSPlugin);
		auto* rollList = Lookup<RE::BGSListForm>(0xD70, kSoSPlugin);
		const bool isRoll = rollList && base && rollList->HasForm(base);
		const bool isBed = bedList && base && bedList->HasForm(base);
		if (!isRoll && !isBed) {
			SKSE::log::info("[spoon] the player's bed {:08X} is not one Spooning of Skyrim supports: no shared spot", base ? base->GetFormID() : 0);
			return nullptr;
		}
		if (auto* quest = Lookup<RE::TESQuest>(0xD63, kSoSPlugin); quest && !quest->refAliasMap.empty()) {
			SKSE::log::info("[spoon] Spooning of Skyrim spooners are assigned with its own spell: it places its own spots, nothing done here");
			return nullptr;
		}

		// Spooner A's settings, exactly as BedQuestScript.PlaceOneBed uses them.
		const int type = static_cast<int>(GlobalValue(0x017, 1.0f));  // HSF_BedTypeA: 0 left, 1 center, 2 right
		RE::FormID spotId = 0;
		if (isRoll) {
			spotId = type == 0 ? 0xD67 : type == 2 ? 0xD68 : 0xD72;  // HSFBedrollLeft / Right / HSFBedroll
		} else {
			spotId = type == 0 ? 0xD62 : type == 2 ? 0x003 : 0xD71;  // HSFBedLeft / HSFBedRight / HSFBed
		}
		auto* spotBase = Lookup<RE::TESFurniture>(spotId, kSoSPlugin);
		if (!spotBase) {
			return nullptr;
		}
		float x = GlobalValue(0x00B, 30.0f);  // HSF_OffsetXA
		float y = GlobalValue(0x00E, 10.0f);  // HSF_OffsetYA
		const float z = GlobalValue(0x011, 0.0f);   // HSF_OffsetZA
		const float extra = GlobalValue(0x014, 0.0f) * std::numbers::pi_v<float> / 180.0f;  // HSF_BedAngleA (degrees)
		float angle = player->data.angle.z;
		if (isRoll) {
			angle += std::numbers::pi_v<float>;
			x = -x;
			y = -y;
		}
		const float dx = y * std::sin(angle) + x * std::cos(angle);
		const float dy = y * std::cos(angle) - x * std::sin(angle);
		const auto  pos = player->GetPosition();

		RemoveSpot("a new bedtime started");
		auto placed = player->PlaceObjectAtMe(spotBase, false);
		if (!placed) {
			return nullptr;
		}
		placed->SetPosition(pos.x + dx, pos.y + dy, pos.z + z);
		placed->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, angle + extra });
		g_spot = placed->GetHandle();
		SKSE::log::info("[spoon] the player lies in a {} nobody shares: Spooning of Skyrim spot {:08X} placed next to the player", isRoll ? "bedroll" : "bed", placed->GetFormID());
		return placed.get();
	}

	void Install()
	{
		if (Patch::GetState() != Patch::State::kActive) {
			return;
		}
		std::thread(Thread).detach();
	}

	void OnLoadingGame()
	{
		g_tagged.clear();
		g_pose = nullptr;
		g_spot = {};
		g_sweep = true;
	}
}
