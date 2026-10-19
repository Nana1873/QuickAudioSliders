#pragma once

namespace QAS::Audio
{
    struct Entry
    {
        RE::BGSSoundCategory* category{ nullptr };  // nullptr = master volume
        RE::FormID            formID{ 0 };
        std::string           editorID;
        std::string           label;     // display name (translated, overridden, or tidied)
        std::string           widgetID;  // unique ImGui label ("Label##formid")
        bool                  labelOverridden{ false };  // from [Labels] in the plugin INI: shown verbatim
        std::string           rawFullName;  // FULL as the record carries it ("$Effects"), for late translation
        // Runtime-created per-mod category (opt-in, see [ModCategories]):
        // no engine slot, persisted only in the plugin INI under persistKey,
        // grouped under ownerFileName.
        bool                  synthetic{ false };
        std::string           ownerFileName;
        std::string           persistKey;
        int                   slot{ -1 };  // [AudioMenu] fVal<slot> / uID<slot>; -1 = master
        float                 uiVolume{ 1.0F };  // last value the UI pushed (render thread)
        std::atomic<bool>     pending{ false };  // a Preview() task is still queued
    };

    // Sliders grouped by the plugin that defines the category. The Vanilla
    // group (Skyrim.esm, DLCs, Creation Club) comes first, then one group per
    // mod plugin sorted by name; entries inside a group are sorted by label.
    struct Group
    {
        std::string              label;     // "Vanilla Game" or the plugin name without extension
        std::string              fileName;  // defining plugin file ("" for the Vanilla group)
        std::string              author;    // TES4 header author, may be empty
        std::string              widgetID;  // unique ImGui label
        bool                     vanilla{ false };
        bool                     misc{ false };  // "Sonstige Mods": mods with fewer than kMinGroupSize sliders
        std::vector<std::size_t> entries;        // indices into the entry list
    };

    // A mod plugin gets its own group only with at least this many sliders;
    // smaller ones are pooled into the collapsed "Sonstige Mods" group at the
    // end, each slider prefixed with its mod name.
    inline constexpr std::size_t kMinGroupSize = 2;

    // kDataLoaded: enumerate menu sound categories, read the [AudioMenu]
    // slot assignment, and snapshot the current volumes.
    void Initialize();

    [[nodiscard]] bool        IsReady();
    [[nodiscard]] std::size_t Count();
    [[nodiscard]] Entry&      Get(std::size_t a_index);
    [[nodiscard]] const std::vector<Group>& Groups();

    // Re-resolves "$Key" labels through the game's translation table. Called
    // once from the first render: SKSE imports the per-plugin translation
    // files after kDataLoaded, so a lookup at Initialize() can still miss.
    void RefreshTranslatedLabels();

    // Current volume as the game holds it (0..1). Plain memory read, any thread.
    [[nodiscard]] float ReadVolume(const Entry& a_entry);

    // Live apply on the game thread (same call path as the Vanilla slider).
    void Preview(std::size_t a_index, float a_volume);

    // Write the entry's current volume: master and engine-slot categories to
    // SkyrimPrefs.ini [AudioMenu], every category additionally to
    // Data/SKSE/Plugins/QuickAudioSliders.ini (the only store for categories
    // beyond the engine's eight slots; those are restored at kDataLoaded).
    void Persist(std::size_t a_index);

    // The value a slider has without any user override: the SNCT record's
    // UNAM "default menu value" (what the engine initialises volumeMult
    // from), 100% for the master and for a record that carries no UNAM.
    [[nodiscard]] float DefaultVolume(const Entry& a_entry);

    // Reset one slider / every slider (and the global multiplier) to the
    // defaults above. Applied live and persisted exactly like a slider
    // release, so the defaults survive a restart.
    void ResetEntry(std::size_t a_index);
    void ResetAll();

    // "All sounds" multiplier (0..1). Applied on the game thread to every
    // root sound category (no parent), as root volume = baseline × global,
    // so the per-category values and their INI lines stay untouched and
    // 100% restores the exact previous state. Persisted in the plugin INI
    // ([Volumes] GlobalMultiplier) and re-applied at kDataLoaded.
    [[nodiscard]] float       GetGlobal();
    [[nodiscard]] std::size_t RootCount();
    void                      SetGlobal(float a_multiplier);
    void                      PersistGlobal();
}
