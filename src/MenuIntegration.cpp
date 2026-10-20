#include "MenuIntegration.h"

#include "AudioSliders.h"
#include "SKSEMenuFramework.h"
#include "Translation.h"

namespace
{
    constexpr auto kSectionName = "Quick Audio Sliders";

    // Two-click confirmation for "reset all": the first click arms the
    // button for a few seconds, the second click within that window resets.
    constexpr double kConfirmSeconds = 4.0;
    double           g_confirmUntil = 0.0;

    void RenderSlider(std::size_t a_index)
    {
        const auto& strings = QAS::Translation::S();
        auto&       entry = QAS::Audio::Get(a_index);
        ImGuiMCP::PushID(static_cast<int>(a_index));
        if (ImGuiMCP::SmallButton(strings.reset.c_str())) {
            QAS::Audio::ResetEntry(a_index);
        }
        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(strings.resetTip.c_str(), QAS::Audio::DefaultVolume(entry) * 100.0F);
        }
        ImGuiMCP::SameLine();
        // While a preview task is still queued, show the value the user
        // dragged to so the knob does not snap back for a frame.
        auto percent = (entry.pending.load() ? entry.uiVolume : QAS::Audio::ReadVolume(entry)) * 100.0F;
        if (ImGuiMCP::SliderFloat(entry.widgetID.c_str(), &percent, 0.0F, 100.0F, "%.0f%%")) {
            QAS::Audio::Preview(a_index, percent / 100.0F);
        }
        if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
            QAS::Audio::Persist(a_index);
        }
        ImGuiMCP::PopID();
    }

    void RenderResetAll()
    {
        const auto& strings = QAS::Translation::S();
        const auto  now = ImGuiMCP::GetTime();
        const bool  armed = now < g_confirmUntil;
        const auto  label = (armed ? strings.resetAllConfirm : strings.resetAll) + "##resetall";
        if (ImGuiMCP::Button(label.c_str())) {
            if (armed) {
                g_confirmUntil = 0.0;
                QAS::Audio::ResetAll();
            } else {
                g_confirmUntil = now + kConfirmSeconds;
            }
        }
        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::SetTooltip(strings.resetAllTip.c_str(), kConfirmSeconds);
        }
    }

    // Shared start of both pages. Returns false when nothing can be drawn yet.
    bool BeginPage()
    {
        const auto& strings = QAS::Translation::S();
        if (!QAS::Audio::IsReady() || QAS::Audio::Count() == 0) {
            ImGuiMCP::TextDisabled("%s", strings.waiting.c_str());
            return false;
        }
        // Translation files imported after kDataLoaded are complete by the
        // time the menu can be opened; retry the labels that missed once.
        static bool labelsRefreshed = false;
        if (!labelsRefreshed) {
            labelsRefreshed = true;
            QAS::Audio::RefreshTranslatedLabels();
        }
        return true;
    }

    // Draws the groups, keeping only menu categories (a_synthetic = false,
    // the "Volume" page) or only runtime mod categories (true, the "Mods"
    // page). A group heading is drawn only when it has sliders on this page.
    std::size_t RenderGroups(bool a_synthetic)
    {
        std::size_t drawn = 0;
        for (const auto& group : QAS::Audio::Groups()) {
            bool headingDrawn = false;
            for (const auto index : group.entries) {
                if (QAS::Audio::Get(index).synthetic != a_synthetic) {
                    continue;
                }
                if (!headingDrawn) {
                    ImGuiMCP::SeparatorText(group.label.c_str());
                    headingDrawn = true;
                }
                RenderSlider(index);
                ++drawn;
            }
        }
        return drawn;
    }

    // Page "Auto Sliders": runtime categories created by [ModCategories].
    void __stdcall RenderMods()
    {
        const auto& strings = QAS::Translation::S();
        if (!BeginPage()) {
            return;
        }
        ImGuiMCP::TextDisabled("%s", strings.modsPageHint.c_str());
        ImGuiMCP::Spacing();
        if (RenderGroups(true) == 0) {
            ImGuiMCP::TextDisabled("%s", strings.modsPageEmpty.c_str());
        }
        ImGuiMCP::Spacing();
        ImGuiMCP::TextDisabled("%s", strings.footer.c_str());
    }

    // Page "Volume": global + reset, then the categories Settings → Audio
    // also shows (Vanilla and mod categories with the menu flag).
    void __stdcall RenderVolume()
    {
        const auto& strings = QAS::Translation::S();
        if (!BeginPage()) {
            return;
        }

        // Global multiplier on top: scales every sound category root (the
        // whole volume tree, menu-visible or not); saved in the plugin INI
        // and applied again on startup. 100% restores the exact previous state.
        ImGuiMCP::SeparatorText("Global");
        {
            auto       percent = QAS::Audio::GetGlobal() * 100.0F;
            const auto label = strings.global + "##global";
            if (ImGuiMCP::SliderFloat(label.c_str(), &percent, 0.0F, 100.0F, "%.0f%%")) {
                QAS::Audio::SetGlobal(percent / 100.0F);
            }
            if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
                QAS::Audio::PersistGlobal();
            }
            ImGuiMCP::SameLine();
            RenderResetAll();
            ImGuiMCP::TextDisabled("%s",
                QAS::Audio::RootCount() == 0 ? strings.noRoots.c_str() : strings.globalHint.c_str());
        }

        // Groups are flat, always-visible sections: Vanilla first, then one
        // section per mod with several sliders, then the pooled single-slider
        // mods. No sub-captions, so every slider is one line.
        RenderGroups(false);

        ImGuiMCP::Spacing();
        ImGuiMCP::TextDisabled("%s", strings.footer.c_str());
    }

    // [ModCategories] Enabled (default 1), read directly at kPostLoad (before
    // AudioSliders resolves its paths) so the "Mods" page is registered only
    // when the feature is on.
    bool ModCategoriesEnabled()
    {
        std::error_code ec;
        const auto      path = std::filesystem::absolute("Data/SKSE/Plugins/QuickAudioSliders.ini", ec);
        if (ec || !std::filesystem::exists(path, ec)) {
            return true;  // no INI yet: the feature is on by default
        }
        return GetPrivateProfileIntA("ModCategories", "Enabled", 1, path.string().c_str()) != 0;
    }
}

namespace QAS::MenuIntegration
{
    void Initialize()
    {
        const auto version = SKSEMenuFramework::GetMenuFrameworkVersion();
        if (version <= 0.0F) {
            logs::warn("SKSE Menu Framework is not installed; no in-game sliders will be shown");
            return;
        }
        SKSEMenuFramework::SetSection(kSectionName);
        SKSEMenuFramework::AddSectionItem("Volume", RenderVolume);
        // Page names are registered here at kPostLoad, before any translation
        // table exists, so they stay English: "Volume" = the regular menu
        // sliders, "Auto Sliders" = the categories this plugin creates.
        const bool autoPage = ModCategoriesEnabled();
        if (autoPage) {
            SKSEMenuFramework::AddSectionItem("Auto Sliders", RenderMods);
        }
        logs::info("Registered SKSE Menu Framework section '{}' (framework v{}), pages: Volume{}", kSectionName,
            version, autoPage ? ", Auto Sliders" : "");
    }
}
