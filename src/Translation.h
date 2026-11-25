#pragma once

namespace QAS::Translation
{
    // Parses Interface/Translations/<a_name>_<sLanguage>.txt (ENGLISH as
    // fallback) into the plugin's own table; the engine's translation table
    // is never written (startup crash, see design-notes 2026-10-27). Game
    // thread only, during kDataLoaded.
    void Import(std::string_view a_name);

    // Resolves a Scaleform translation key such as "$Effects": first the
    // plugin's own table (see Import), then read-only through the game's
    // translator (the strings the Vanilla audio menu shows). Falls back to
    // a_fallback when given, else to the key without its leading '$'.
    std::string Translate(std::string_view a_key, std::string_view a_fallback = {});

    // The plugin's own UI strings. English defaults live here; a
    // Data/Interface/Translations/QuickAudioSliders_<LANGUAGE>.txt file
    // overrides them (parsed by Import at kDataLoaded). Resolved once by
    // LoadStrings().
    struct Strings
    {
        std::string globalHeading = "Global";
        std::string global = "All Sounds (global)";
        std::string noRoots = "No root sound categories found; the global slider has no effect.";
        std::string resetAll = "Reset All";
        std::string resetAllConfirm = "Confirm?";  // armed button label (two-click confirmation)
        std::string resetAllTip = "Resets all sliders on both pages.";
        std::string modsPageHint = "Extra sliders for mods that have no volume slider of their own.";
        std::string modsPageEmpty = "No mod qualified (needs at least MinSounds eligible sound descriptors).";
        std::string reset = "Reset";
        // resetTip is a printf format for an ImGui tooltip; a translation is
        // only accepted when its placeholders match this default (LoadStrings
        // validates and falls back otherwise).
        std::string resetTip = "Reset to %.0f%% (plugin default)";
        std::string vanillaGame = "Vanilla Game";
        std::string otherMods = "Other Mods";
        std::string footer = "Changes apply immediately and are saved.";
        std::string waiting = "Waiting for game data to load...";
        std::string noCategories = "No sound categories with a menu slider found in this load order.";
        // Page "Now Playing"
        std::string nowPlayingHint = "Sounds the game requested since you entered this place, each with the slider that controls it: loops first, then single sounds from the last two minutes.";
        std::string nowPlayingEmpty = "No sound was requested in the last two minutes.";
        std::string nowPlayingLimits = "A listed loop may have stopped since. Voice lines and music started by file name are not listed.";
        std::string colSound = "Sound";
        std::string colMod = "Mod";
        std::string colSlider = "Slider";
        std::string colAge = "Ago";
        std::string noSlider = "none (global only)";
        std::string extraPageTag = "Extra Sliders";
        std::string sectionLoops = "Loops";
        std::string sectionSingle = "Single sounds";
    };

    void           LoadStrings();
    const Strings& S();
}
