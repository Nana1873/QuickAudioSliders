#pragma once

namespace QAS::Translation
{
    // Resolves a Scaleform translation key such as "$Effects" through the
    // game's own translator (the strings the Vanilla audio menu shows).
    // Falls back to a_fallback when given, else to the key without its
    // leading '$'.
    std::string Translate(std::string_view a_key, std::string_view a_fallback = {});

    // The plugin's own UI strings. English defaults live here; a
    // Data/Interface/Translations/QuickAudioSliders_<LANGUAGE>.txt file
    // overrides them (SKSE::Translation::ParseTranslation, loaded at
    // kDataLoaded). Resolved once by LoadStrings().
    struct Strings
    {
        std::string global = "All Sounds (global)";
        std::string globalHint = "Multiplies every sound category; saved and applied again on startup.";
        std::string noRoots = "No root sound categories found; the global slider has no effect.";
        std::string resetAll = "Reset All";
        std::string resetAllConfirm = "Really reset all? (click again)";
        std::string resetAllTip =
            "Resets every category on both pages to its plugin default and the global slider and Vanilla master "
            "to 100%%. Two clicks within %.0f seconds.";
        std::string modsPageHint =
            "Sliders this plugin creates for mods whose sounds only use Vanilla categories ([ModCategories] in "
            "QuickAudioSliders.ini). The global slider and Reset All on the Volume page apply here too.";
        std::string modsPageEmpty = "No mod qualified (needs at least MinSounds eligible sound descriptors).";
        std::string reset = "Reset";
        std::string resetTip = "Reset to %.0f%% (plugin default)";
        std::string vanillaGame = "Vanilla Game";
        std::string otherMods = "Other Mods";
        std::string footer = "Changes apply immediately and are saved.";
        std::string waiting = "Waiting for game data to load...";
    };

    void           LoadStrings();
    const Strings& S();
}
