#include "PCH.h"

#include "Camera.h"

#include "Settings.h"

namespace Camera
{
	namespace
	{
		std::atomic<bool> g_tickQueued{ false };

		bool        g_lying = false;
		std::int32_t g_lastState = -1;
		int         g_forced = 0;

		const char* StateName(std::int32_t a_id)
		{
			switch (a_id) {
			case RE::CameraStates::kFirstPerson:
				return "first person";
			case RE::CameraStates::kAutoVanity:
				return "auto vanity";
			case RE::CameraStates::kVATS:
				return "VATS";
			case RE::CameraStates::kFree:
				return "free";
			case RE::CameraStates::kIronSights:
				return "iron sights";
			case RE::CameraStates::kFurniture:
				return "furniture";
			case RE::CameraStates::kPCTransition:
				return "transition";
			case RE::CameraStates::kTween:
				return "tween";
			case RE::CameraStates::kAnimated:
				return "animated";
			case RE::CameraStates::kThirdPerson:
				return "third person";
			case RE::CameraStates::kMount:
				return "mount";
			case RE::CameraStates::kBleedout:
				return "bleedout";
			case RE::CameraStates::kDragon:
				return "dragon";
			default:
				return "?";
			}
		}

		std::int32_t CurrentState(RE::PlayerCamera* a_camera)
		{
			auto* state = a_camera ? a_camera->currentState.get() : nullptr;
			return state ? static_cast<std::int32_t>(state->id) : -1;
		}

		void Tick()
		{
			g_tickQueued = false;
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* camera = RE::PlayerCamera::GetSingleton();
			if (!player || !camera) {
				return;
			}
			const bool lying = player->AsActorState()->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping;
			if (!lying) {
				if (g_lying) {
					SKSE::log::info("[camera] player got up; camera in {}; forced back to third person {} time(s) while in bed", StateName(CurrentState(camera)), g_forced);
				}
				g_lying = false;
				g_lastState = -1;
				g_forced = 0;
				return;
			}
			const auto state = CurrentState(camera);
			if (!g_lying) {
				g_lying = true;
				SKSE::log::info("[camera] player lies in bed; camera in {} (option third person in bed: {})", StateName(state), Settings::Get().thirdPersonInBed);
			} else if (state != g_lastState) {
				SKSE::log::info("[camera] in bed: camera {} -> {}", StateName(g_lastState), StateName(state));
			}
			g_lastState = state;
			if (Settings::Get().thirdPersonInBed && camera->IsInFirstPerson()) {
				camera->ForceThirdPerson();
				++g_forced;
				if (g_forced <= 10) {
					SKSE::log::info("[camera] in bed: something switched to first person, forced back to third person ({})", g_forced);
				}
			}
		}

		void Thread()
		{
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
		if (REL::Module::IsVR()) {
			SKSE::log::info("[camera] Skyrim VR: the bed camera option does not apply (VR is always first person)");
			return;
		}
		std::thread(Thread).detach();
		SKSE::log::info("[camera] bed camera watch started");
	}
}
