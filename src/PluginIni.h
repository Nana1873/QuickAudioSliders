#pragma once

// Shared plugin-INI constants. MenuIntegration reads [ModCategories] Enabled
// already at kPostLoad, before AudioSliders resolves its paths at kDataLoaded,
// so both translation units take the names from here.
namespace QAS::Ini
{
    // Relative to the game CWD; resolve to an absolute path before use (the
    // Win32 profile API looks in the Windows directory for a bare file name).
    inline constexpr auto kPluginIniRelative = "Data/SKSE/Plugins/QuickAudioSliders.ini";
    inline constexpr auto kModCategoriesSection = "ModCategories";
    inline constexpr auto kModCategoriesEnabledKey = "Enabled";
    inline constexpr bool kModCategoriesEnabledDefault = true;
}
