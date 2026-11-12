#include "PCH.h"

#include "Menu.h"

#include "Patch.h"
#include "Settings.h"

#include "SKSEMenuFramework.h"

namespace Menu
{
	namespace
	{
		namespace ui = ImGuiMCP;

		void __stdcall RenderPage()
		{
			ui::SeparatorText("Bed partner patch");

			switch (Patch::GetState()) {
			case Patch::State::kActive:
				ui::TextWrapped("%s", "Active: patching Sleep In Bed SKSE 1.4.");
				break;
			case Patch::State::kMissing:
				ui::TextWrapped("%s", "Not active: SleepInBedSKSE.dll is not loaded. Install Sleep In Bed SKSE 1.4 (Nexus 189803).");
				return;
			case Patch::State::kWrongVersion:
				ui::TextWrapped("%s", "Not active: this patch only supports Sleep In Bed SKSE 1.4. Your SleepInBedSKSE.dll is a different version, so nothing was changed.");
				return;
			case Patch::State::kHookFailed:
				ui::TextWrapped("%s", "Not active: the hooks could not be installed. See SleepInBedPartner.log.");
				return;
			default:
				ui::TextWrapped("%s", "Not active yet.");
				return;
			}

			int partner = static_cast<int>(Settings::Get().bedPartner);
			if (ui::Combo("Who shares the bed", &partner, "Any follower\0Opposite sex\0Same sex\0\0")) {
				Settings::Edit().bedPartner = partner;
				Settings::Save();
			}
			ui::SetItemTooltip("%s", "Which followers may lie down in the player's bed. Any follower: every follower qualifies. Opposite sex: only followers of the other sex than the player. Same sex: only followers of the player's sex. When more than one qualifies, one is picked at random each time; the others use other free beds nearby. A spouse who shares the bed is not affected.");
			ui::TextWrapped("%s", "Works with \"Followers share the player's bed\" on the Settings page. With that option off, only the spouse shares your bed and this choice does nothing.");

			ui::SeparatorText("Camera");
			bool third = Settings::Get().thirdPersonInBed;
			if (ui::Checkbox("Third person while in bed", &third)) {
				Settings::Edit().thirdPersonInBed = third;
				Settings::Save();
			}
			ui::SetItemTooltip("%s", "While you lie in bed, the camera stays in third person, so you can look around the room. If anything switches it to first person, it is switched back (and SleepInBedPartner.log says when).");
		}
	}

	void Install()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::info("[menu] SKSE Menu Framework not installed, no menu page");
			return;
		}
		SKSEMenuFramework::SetSection("Sleep In Bed");
		SKSEMenuFramework::AddSectionItem("Bed Partner", RenderPage);
		SKSE::log::info("[menu] page added: Sleep In Bed > Bed Partner");
	}
}
