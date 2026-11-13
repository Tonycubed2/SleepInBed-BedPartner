#include "PCH.h"

#include "Eyes.h"

// Sleep In Bed 1.4 opens the eyes once, in its SitSleepStateChanged hook, at the moment the sleeper's
// state leaves "sleeping" (start of getting up): it resets the face once and never looks again. The
// get-up animation still runs for a few seconds after that, and if the eyes get closed again meanwhile
// they stay closed until a reload (reported on Nexus). This watches each sleeper while getting up and
// for a few seconds after standing: eyes still fully closed for longer than a blink -> opened again
// with the same face reset Sleep In Bed uses, and logged.

namespace Eyes
{
	namespace
	{
		constexpr float kWatchAfterStanding = 8.0f;  // seconds after reaching normal state
		constexpr float kClosedTooLong = 1.5f;       // a blink is ~0.2 s
		constexpr float kClosed = 0.9f;              // blink modifier value counted as closed

		using Clock = std::chrono::steady_clock;

		struct Sleeper
		{
			bool              inBed{ false };
			bool              watching{ false };
			Clock::time_point standing{};
			bool              closed{ false };
			Clock::time_point closedSince{};
			int               reopened{ 0 };
		};

		std::unordered_map<RE::FormID, Sleeper> g_sleepers;  // main thread only
		std::mutex                              g_trackLock;
		std::vector<RE::FormID>                 g_toTrack;
		std::atomic<bool>                       g_tickQueued{ false };

		const char* NameOf(RE::Actor* a_actor)
		{
			const char* name = a_actor ? a_actor->GetDisplayFullName() : nullptr;
			return name && *name ? name : "?";
		}

		float Seconds(Clock::time_point a_since)
		{
			return std::chrono::duration<float>(Clock::now() - a_since).count();
		}

		float BlinkValue(RE::BSFaceGenAnimationData* a_face)
		{
			return a_face->modifier3.count > 0 ? a_face->modifier3.values[0] : -1.0f;
		}

		void Reopen(RE::Actor* a_actor, RE::BSFaceGenAnimationData* a_face, Sleeper& a_sleeper)
		{
			++a_sleeper.reopened;
			SKSE::log::info("[eyes] {:08X} ({}) eyes still closed {:.1f} s after getting up (blink={:.2f} hold={} stage={}): opened again", a_actor->GetFormID(), NameOf(a_actor), Seconds(a_sleeper.closedSince), BlinkValue(a_face), a_face->unk21A, static_cast<std::uint32_t>(a_face->eyesBlinkingStage));
			a_face->unk21A = 0;
			a_face->Reset(0.0f, false, true, false, false);
			a_face->eyesBlinkingStage = RE::BSFaceGenAnimationData::EyesBlinkingStage::BlinkDelay;
			a_face->eyesBlinkingTimer = 0.0f;
			a_sleeper.closed = false;
		}

		void Check(RE::Actor* a_actor, Sleeper& a_sleeper)
		{
			const auto state = a_actor->AsActorState()->GetSitSleepState();
			if (state == RE::SIT_SLEEP_STATE::kIsSleeping) {
				a_sleeper.inBed = true;
				a_sleeper.watching = false;
				a_sleeper.closed = false;
				a_sleeper.reopened = 0;
				return;
			}
			if (a_sleeper.inBed) {
				// Getting up (wake / get-up animation) or already standing.
				a_sleeper.inBed = false;
				a_sleeper.watching = true;
				a_sleeper.standing = Clock::time_point{};
			}
			if (!a_sleeper.watching) {
				return;
			}
			if (state == RE::SIT_SLEEP_STATE::kNormal && a_sleeper.standing == Clock::time_point{}) {
				a_sleeper.standing = Clock::now();
			}
			if (a_sleeper.standing != Clock::time_point{} && Seconds(a_sleeper.standing) > kWatchAfterStanding) {
				a_sleeper.watching = false;
				return;
			}
			auto* face = a_actor->GetFaceGenAnimationData();
			if (!face) {
				return;
			}
			const bool closed = BlinkValue(face) >= kClosed;
			if (!closed) {
				a_sleeper.closed = false;
				return;
			}
			if (!a_sleeper.closed) {
				a_sleeper.closed = true;
				a_sleeper.closedSince = Clock::now();
				return;
			}
			if (Seconds(a_sleeper.closedSince) >= kClosedTooLong && a_sleeper.reopened < 5) {
				Reopen(a_actor, face, a_sleeper);
			}
		}

		void Tick()
		{
			g_tickQueued = false;
			{
				std::scoped_lock lock(g_trackLock);
				for (auto id : g_toTrack) {
					g_sleepers.try_emplace(id);
				}
				g_toTrack.clear();
			}
			g_sleepers.try_emplace(0x14);
			for (auto it = g_sleepers.begin(); it != g_sleepers.end();) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
				if (!actor || actor->IsDead()) {
					it = it->first == 0x14 ? std::next(it) : g_sleepers.erase(it);
					continue;
				}
				if (actor->Is3DLoaded()) {
					Check(actor, it->second);
				}
				++it;
			}
		}

		void Thread()
		{
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
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

	void Track(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		std::scoped_lock lock(g_trackLock);
		g_toTrack.push_back(a_actor->GetFormID());
	}

	void Install()
	{
		std::thread(Thread).detach();
		SKSE::log::info("[eyes] watching sleepers' eyes after getting up");
	}
}
