#include "AudioSliders.h"

#include "PluginIni.h"
#include "StringConv.h"
#include "Translation.h"

namespace
{
    constexpr auto kSection = "AudioMenu";
    constexpr auto kMasterSetting = "fAudioMasterVolume:AudioMenu";
    constexpr int  kMaxSlots = 64;

    // Plugin-owned volume store for categories the engine cannot persist
    // (it registers only uID0..uID7 / fVal0..fVal7 and restores only those).
    // Keyed by "<defining plugin>|<local FormID>", so a load-order change does
    // not detach a value from its category. Path and [ModCategories] names are
    // shared with MenuIntegration via PluginIni.h.
    constexpr auto kPluginIniRelative = QAS::Ini::kPluginIniRelative;
    constexpr auto kVolumesSection = "Volumes";
    constexpr auto kLabelsSection = "Labels";  // plugin INI: <plugin>|<local FormID> = display name
    constexpr auto kModCategoriesSection = QAS::Ini::kModCategoriesSection;
    constexpr auto kSrdFolderRelative = "Data/SKSE/Plugins/SoundRecordDistributor";
    constexpr auto kGlobalKey = "GlobalMultiplier";    // [Volumes] key of the "Alle Sounds" slider
    constexpr auto kMasterRepairedKey = "MasterRepaired";  // [Volumes] marker: startup master repair done once
    constexpr auto kOwnsAllKey = "StoreOwnsAllVolumes";  // [Volumes] marker: the store also restores engine-slot categories
    constexpr auto kVanillaMasterKey = "VanillaMaster";  // [Volumes] copy of fAudioMasterVolume
    constexpr int  kEngineSlotsFallback = 8;  // assumed when no uID<n> setting is registered
    constexpr float kVolumeEpsilon = 0.0005F;  // two volumes closer than this are the same value

    std::vector<std::unique_ptr<QAS::Audio::Entry>> g_entries;
    std::vector<QAS::Audio::Group>                  g_groups;
    std::atomic<bool>                               g_ready{ false };
    std::string                                     g_prefsPath;
    std::string                                     g_pluginIniPath;
    // Second home of the volume store, used when the plugin INI cannot be
    // written (see "volume store" below): <My Games>/SKSE/QuickAudioSliders.ini.
    std::string                                     g_fallbackIniPath;
    bool                                            g_fallbackActive{ false };  // the fallback file holds values
    bool                                            g_fallbackWarned{ false };
    float                                           g_savedMaster{ -1.0F };  // Vanilla master as last stored / found; < 0 = unknown
    int                                             g_engineSlots{ kEngineSlotsFallback };

    float Clamp01(float a_value);
    void  BroadcastVolumeChanged();

    // Global multiplier state. g_global is what the UI shows (any thread);
    // g_globalActive and the baselines are written on the game thread only
    // and read relaxed from the render thread (a stale value costs a frame).
    struct RootCategory
    {
        RE::BGSSoundCategory* category{ nullptr };
        std::string           editorID;
        std::atomic<float>    baseline{ 1.0F };

        RootCategory() = default;
        RootCategory(RootCategory&& a_rhs) noexcept :
            category(a_rhs.category),
            editorID(std::move(a_rhs.editorID)),
            baseline(a_rhs.baseline.load(std::memory_order_relaxed))
        {}
    };
    std::vector<RootCategory> g_roots;
    std::atomic<float>        g_global{ 1.0F };
    std::atomic<bool>         g_globalActive{ false };
    std::atomic<bool>         g_globalDirty{ false };  // SetGlobal() without PersistGlobal() yet

    constexpr float kGlobalIdentity = 0.9995F;  // >= this counts as 100%

    // Engine-managed category names we never touch, even if one ever shows
    // up as a root.
    bool IsEngineManagedCategory(std::string_view a_editorID)
    {
        static constexpr std::array kBlocked{ "PausedDuringMenu"sv, "FadeDuringDialogue"sv, "MuteSubmerged"sv,
            "MuteWhenSubmerged"sv };
        for (const auto blocked : kBlocked) {
            if (a_editorID.find(blocked) != std::string_view::npos) {
                return true;
            }
        }
        return false;
    }

    // Every sound category that has no parent is a root of the volume tree:
    // scaling the roots scales every sound exactly once (children multiply
    // their parent's volume), menu-visible or not.
    void CollectRoots(RE::TESDataHandler* a_dataHandler)
    {
        g_roots.clear();
        for (auto* category : a_dataHandler->GetFormArray<RE::BGSSoundCategory>()) {
            if (!category || category->parentCategory) {
                continue;
            }
            const char* editorID = category->GetFormEditorID();
            RootCategory root;
            root.category = category;
            root.editorID = editorID ? editorID : std::format("{:08X}", category->GetFormID());
            if (IsEngineManagedCategory(root.editorID)) {
                logs::info("Root category {:08X} '{}' skipped for the global slider (engine-managed)",
                    category->GetFormID(), root.editorID);
                continue;
            }
            const float baseline = category->volumeMult;
            root.baseline.store(baseline, std::memory_order_relaxed);
            logs::info("Root category {:08X} '{}' volumeMult {:.4f}{}", category->GetFormID(), root.editorID,
                baseline, category->IsMenuCategory() ? " (menu)" : "");
            g_roots.push_back(std::move(root));
        }
        g_globalActive.store(false, std::memory_order_relaxed);
        g_global.store(1.0F);
    }

    // Game thread only.
    void ApplyGlobalNow(float a_multiplier)
    {
        const bool identity = a_multiplier >= kGlobalIdentity;
        if (!g_globalActive.load(std::memory_order_relaxed)) {
            if (identity) {
                return;  // nothing to do, and nothing of ours to undo
            }
            // Taking control: remember what the engine / other mods set.
            for (auto& root : g_roots) {
                root.baseline.store(root.category->volumeMult, std::memory_order_relaxed);
            }
            g_globalActive.store(true, std::memory_order_relaxed);
        }
        for (auto& root : g_roots) {
            const float baseline = root.baseline.load(std::memory_order_relaxed);
            root.category->SetCategoryVolume(identity ? baseline : Clamp01(baseline * a_multiplier));
        }
        if (identity) {
            g_globalActive.store(false, std::memory_order_relaxed);  // released: baselines are live again
        }
        BroadcastVolumeChanged();
    }

    bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs)
    {
        return a_lhs.size() == a_rhs.size() &&
               _strnicmp(a_lhs.data(), a_rhs.data(), a_lhs.size()) == 0;
    }

    bool StartsWithNoCase(std::string_view a_text, std::string_view a_prefix)
    {
        return a_text.size() >= a_prefix.size() &&
               _strnicmp(a_text.data(), a_prefix.data(), a_prefix.size()) == 0;
    }

    bool EndsWithNoCase(std::string_view a_text, std::string_view a_suffix)
    {
        return a_text.size() >= a_suffix.size() &&
               _strnicmp(a_text.data() + (a_text.size() - a_suffix.size()), a_suffix.data(), a_suffix.size()) == 0;
    }

    std::string LowerCopy(std::string_view a_text)
    {
        std::string result(a_text);
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char a_char) { return static_cast<char>(std::tolower(a_char)); });
        return result;
    }

    // Base game, official DLC, Creation Club content and the Unofficial Patch
    // count as "Vanilla" (none of them is a mod the user would look for).
    bool IsVanillaFile(std::string_view a_fileName)
    {
        static constexpr std::array kOfficial{
            "Skyrim.esm"sv, "Update.esm"sv, "Dawnguard.esm"sv, "HearthFires.esm"sv, "Dragonborn.esm"sv,
            "_ResourcePack.esl"sv, "unofficial skyrim special edition patch.esp"sv
        };
        for (const auto official : kOfficial) {
            if (EqualsNoCase(a_fileName, official)) {
                return true;
            }
        }
        // Creation Club: ccBGSSSE001-Fish.esm, ccQDRSSE001-SurvivalMode.esl, ...
        return StartsWithNoCase(a_fileName, "cc"sv) &&
               (EndsWithNoCase(a_fileName, ".esm"sv) || EndsWithNoCase(a_fileName, ".esl"sv));
    }

    std::string StripExtension(std::string_view a_fileName)
    {
        const auto dot = a_fileName.find_last_of('.');
        return std::string(dot == std::string_view::npos ? a_fileName : a_fileName.substr(0, dot));
    }

    std::string Trim(std::string a_text)
    {
        while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.back()))) {
            a_text.pop_back();
        }
        while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.front()))) {
            a_text.erase(a_text.begin());
        }
        return a_text;
    }

    // FULL names of the Skyrim.esm sound categories as the base game ships
    // them (the DLCs define none). A Vanilla category keeps its "Vanilla
    // Game" slot as long as its displayed name is still this one; a mod that
    // renames it (AOS: AMB -> "$AOS_Ambience", MAG -> "$AOS_Magic", FSTnpc ->
    // "$AOS_FootstepsNPC") has visibly taken it over and gets the slider.
    // Merely touching or re-flagging a category does not move it.
    std::optional<std::string_view> VanillaFullName(RE::FormID a_formID)
    {
        static constexpr std::array<std::pair<RE::FormID, std::string_view>, 18> kNames{ {
            { 0x000F72, "NPC Footsteps"sv }, { 0x000E9E, "AudioCategorySFXFadeDuringDialogue"sv },
            { 0x0172A1, "$Effects"sv }, { 0x10AA60, "AudioCategoryPausedDuringMenuLoad"sv },
            { 0x0F5FFC, "$Footsteps"sv }, { 0x0F23BB, "AudioCategoryMuteSubmerged"sv },
            { 0x0EB803, "$Master"sv }, { 0x0EA51F, "AudioCategoryNPCKillMove"sv },
            { 0x0DDDC5, "AudioCategoryPausedDuringMenu"sv }, { 0x01A0BD, "Magic"sv },
            { 0x09F254, "AudioCategoryPausedDuringMenuFade"sv }, { 0x0876BD, "$Voice"sv },
            { 0x0876BC, "AudioCategoryNonVOC"sv }, { 0x0876BB, "AudioCategoryVOC"sv },
            { 0x07F80B, "AudioCategoryAMB"sv }, { 0x071E64, "$Music"sv },
            { 0x071E63, "AudioCategoryAMBr"sv }, { 0x064451, "Menus"sv },
        } };
        for (const auto& [id, name] : kNames) {
            if (id == a_formID) {
                return name;
            }
        }
        return std::nullopt;
    }

    // The mod that "owns" a category for grouping, or nullptr for Vanilla.
    // - Defined by a mod: that mod (first file of the override chain).
    // - Defined by an official file: Vanilla unless a mod changed the FULL
    //   name; then the first non-official file in the override chain.
    const RE::TESFile* OwningModFile(const RE::BGSSoundCategory& a_category)
    {
        const auto* files = a_category.sourceFiles.array;
        if (!files || files->empty()) {
            return nullptr;
        }
        const auto* defining = files->front();
        const bool  officialDefinition = !defining || IsVanillaFile(defining->GetFilename());
        if (officialDefinition) {
            const char*      fullName = a_category.GetFullName();
            std::string_view current{ fullName ? fullName : "" };
            // Local FormID (official masters load at index 00..04 and have no
            // ESL slot, so masking the top byte is enough here).
            const auto original = VanillaFullName(a_category.GetFormID() & 0x00FFFFFF);
            // Compare translation keys, not the '$' prefix: AOS turns the
            // plain "Menus" into the key "$Menus" (same text, now
            // translatable) — that is not a takeover.
            auto stripKey = [](std::string_view a_text) {
                return a_text.starts_with('$') ? a_text.substr(1) : a_text;
            };
            if (!original || EqualsNoCase(stripKey(current), stripKey(*original))) {
                return nullptr;
            }
        }
        for (const auto* file : *files) {
            if (file && !IsVanillaFile(file->GetFilename())) {
                return file;
            }
        }
        return nullptr;
    }

    // "Babbling Brooks - A Water Audio Overhaul" -> "Babbling Brooks";
    // "Crackling Fire Volume Slider" -> "Crackling Fire".
    std::string ShortModName(std::string_view a_fileName)
    {
        auto name = StripExtension(a_fileName);
        if (const auto dash = name.find(" - "); dash != std::string::npos) {
            name.erase(dash);
        }
        for (const auto suffix : { " Volume Slider"sv, " Volume Sliders"sv, " SE"sv, " SSE"sv }) {
            if (EndsWithNoCase(name, suffix)) {
                name.erase(name.size() - suffix.size());
            }
        }
        return Trim(name);
    }

    std::string Squash(std::string_view a_text)
    {
        std::string result;
        for (const unsigned char ch : a_text) {
            if (std::isalnum(ch)) {
                result.push_back(static_cast<char>(std::tolower(ch)));
            }
        }
        return result;
    }

    // ---- label cosmetics ----

    // "RÜSTUNG" -> "Rüstung", "WAFFEN" -> "Waffen". Only labels that are
    // entirely upper case (at least one letter, more than three characters)
    // are touched; mixed-case names and short acronyms stay as they are.
    // Case mapping goes through the Win32 Unicode API, so umlauts work.
    std::string TitleCaseIfAllCaps(const std::string& a_label)
    {
        // A FULL that is not valid UTF-8 (legacy 1252-encoded ESPs) is left
        // alone rather than mangled to U+FFFD by the conversion round trip.
        if (!a_label.empty() &&
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_label.data(), static_cast<int>(a_label.size()),
                nullptr, 0) == 0) {
            return a_label;
        }
        auto wide = QAS::Text::ToWide(a_label);
        if (wide.size() <= 3) {
            return a_label;
        }
        int letters = 0;
        for (const auto ch : wide) {
            if (IsCharAlphaW(ch)) {
                ++letters;
                if (IsCharLowerW(ch)) {
                    return a_label;  // mixed case: leave alone
                }
            }
        }
        if (letters == 0) {
            return a_label;
        }
        CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
        bool startOfWord = true;
        for (auto& ch : wide) {
            if (IsCharAlphaW(ch)) {
                if (startOfWord) {
                    wchar_t one[2]{ ch, L'\0' };
                    CharUpperBuffW(one, 1);
                    ch = one[0];
                }
                startOfWord = false;
            } else {
                startOfWord = (ch == L' ' || ch == L'-' || ch == L'_' || ch == L'/' || ch == L'(');
            }
        }
        return QAS::Text::ToUtf8(wide);
    }

    // "AOS_Ambience" under group "Audio Overhaul Skyrim" -> "Ambience".
    // Strips a leading acronym of the mod's words ("AOS") or the short mod
    // name itself, when followed by a separator; generic, no mod names.
    std::string StripModPrefix(const std::string& a_label, std::string_view a_modFileName)
    {
        const auto  shortName = ShortModName(a_modFileName);
        std::string acronym;
        bool        startOfWord = true;
        for (const unsigned char ch : shortName) {
            if (std::isalnum(ch)) {
                if (startOfWord) {
                    acronym.push_back(static_cast<char>(std::toupper(ch)));
                }
                startOfWord = false;
            } else {
                startOfWord = true;
            }
        }
        for (const std::string_view prefix : { std::string_view(acronym), std::string_view(shortName) }) {
            if (prefix.size() < 2 || a_label.size() <= prefix.size() + 1 || !StartsWithNoCase(a_label, prefix)) {
                continue;
            }
            const char separator = a_label[prefix.size()];
            if (separator == '_' || separator == ' ' || separator == '-' || separator == ':' || separator == '.') {
                return Trim(a_label.substr(prefix.size() + 1));
            }
        }
        return a_label;
    }

    // Label for a slider pooled into "Other Mods": the category name alone
    // when it already names the mod, otherwise "<short mod>: <category>".
    std::string PooledLabel(std::string_view a_modFileName, std::string_view a_label)
    {
        const auto mod = ShortModName(a_modFileName);
        const auto squashedMod = Squash(mod);
        const auto squashedLabel = Squash(a_label);
        if (squashedMod.empty() || squashedLabel.find(squashedMod) != std::string::npos ||
            squashedMod.find(squashedLabel) != std::string::npos) {
            return squashedLabel.size() >= squashedMod.size() ? std::string(a_label) : mod;
        }
        return std::format("{}: {}", mod, a_label);
    }

    void BuildGroups()
    {
        g_groups.clear();
        QAS::Audio::Group vanilla;
        vanilla.label = QAS::Translation::S().vanillaGame;
        vanilla.vanilla = true;
        g_groups.push_back(std::move(vanilla));

        for (std::size_t index = 0; index < g_entries.size(); ++index) {
            const auto& entry = *g_entries[index];
            // Runtime-created categories have no file, so this is nullptr for
            // them; a mod's unlisted category still yields its author.
            const auto* file = entry.category ? OwningModFile(*entry.category) : nullptr;
            const auto  fileName = entry.synthetic ? entry.ownerFileName : (file ? std::string(file->GetFilename()) : std::string());

            if (fileName.empty()) {
                g_groups.front().entries.push_back(index);
                continue;
            }

            // Drop a redundant mod prefix from the label ("AOS_Ambience" in
            // the Audio Overhaul group), unless the user overrode the label.
            if (!entry.labelOverridden) {
                auto& mutableEntry = *g_entries[index];
                mutableEntry.label = StripModPrefix(mutableEntry.label, fileName);
                mutableEntry.labelModFile = fileName;
            }

            auto it = std::find_if(g_groups.begin(), g_groups.end(),
                [&](const QAS::Audio::Group& a_group) { return EqualsNoCase(a_group.fileName, fileName); });
            if (it == g_groups.end()) {
                QAS::Audio::Group group;
                group.fileName = fileName;
                group.label = StripExtension(fileName);
                // Volume Sliders for Weather defines its categories in
                // "... - Vanilla.esp" even with a weather-mod patch installed;
                // the file name would read as "Vanilla only".
                if (EqualsNoCase(fileName, "Volume Sliders for Weather - Vanilla.esp"sv)) {
                    group.label = "Volume Sliders for Weather";
                }
                // "DEFAULT" is the Creation Kit's placeholder author; hide it.
                if (file) {
                    if (const char* author = file->createdBy.c_str();
                        author && *author && !EqualsNoCase(author, "DEFAULT"sv)) {
                        group.author = author;
                    }
                }
                g_groups.push_back(std::move(group));
                it = std::prev(g_groups.end());
            }
            it->entries.push_back(index);
        }

        // Mods with fewer than kMinGroupSize sliders are pooled into one
        // collapsed group; their sliders get a "<mod>: " prefix so the origin
        // stays visible. Counted per page: a mod with one Volume slider and
        // one Extra slider would otherwise get a one-slider heading on each.
        QAS::Audio::Group misc;
        misc.label = QAS::Translation::S().otherMods;
        misc.misc = true;
        for (auto it = std::next(g_groups.begin()); it != g_groups.end();) {
            const auto extra = static_cast<std::size_t>(std::count_if(it->entries.begin(), it->entries.end(),
                [](std::size_t a_index) { return g_entries[a_index]->synthetic; }));
            const auto menu = it->entries.size() - extra;
            std::erase_if(it->entries, [&](std::size_t a_index) {
                auto& entry = *g_entries[a_index];
                if ((entry.synthetic ? extra : menu) >= QAS::Audio::kMinGroupSize) {
                    return false;
                }
                if (!entry.labelOverridden) {
                    entry.label = PooledLabel(it->fileName, entry.label);
                    entry.labelPooled = true;
                }
                misc.entries.push_back(a_index);
                return true;
            });
            it = it->entries.empty() ? g_groups.erase(it) : std::next(it);
        }

        // Display tidy-up and final widget IDs for every slider.
        for (auto& entry : g_entries) {
            if (!entry->labelOverridden) {
                entry->label = TitleCaseIfAllCaps(entry->label);
            }
            entry->widgetID = std::format("{}##{:08X}", entry->label, entry->formID);
        }

        // Vanilla stays first; mod groups alphabetical by label; misc last.
        std::sort(std::next(g_groups.begin()), g_groups.end(),
            [](const QAS::Audio::Group& a_lhs, const QAS::Audio::Group& a_rhs) {
                return LowerCopy(a_lhs.label) < LowerCopy(a_rhs.label);
            });
        if (!misc.entries.empty()) {
            g_groups.push_back(std::move(misc));
        }
        for (auto& group : g_groups) {
            std::sort(group.entries.begin(), group.entries.end(), [](std::size_t a_lhs, std::size_t a_rhs) {
                const auto lhs = LowerCopy(g_entries[a_lhs]->label);
                const auto rhs = LowerCopy(g_entries[a_rhs]->label);
                return lhs != rhs ? lhs < rhs : g_entries[a_lhs]->formID < g_entries[a_rhs]->formID;
            });
            logs::info("Group '{}' ({}{}): {} slider(s)", group.label,
                group.vanilla ? "official files" : group.misc ? "mods with a single slider" : group.fileName,
                group.author.empty() ? "" : std::format(", author '{}'", group.author),
                group.entries.size());
        }
    }

    float Clamp01(float a_value)
    {
        return std::clamp(a_value, 0.0F, 1.0F);
    }

    RE::Setting* FindPref(std::string_view a_name)
    {
        auto* prefs = RE::INIPrefSettingCollection::GetSingleton();
        return prefs ? prefs->GetSetting(a_name) : nullptr;
    }

    // The INI file the game writes its preference settings to. The engine
    // keeps the path in the collection's subKey; fall back to the My Games
    // folder next to the SKSE log directory.
    std::string ResolvePrefsPath()
    {
        if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
            const std::string_view subKey{ prefs->subKey, strnlen_s(prefs->subKey, sizeof(prefs->subKey)) };
            if (!subKey.empty() && std::filesystem::exists(subKey)) {
                return std::string(subKey);
            }
            if (!subKey.empty()) {
                logs::warn("INIPrefSettingCollection subKey '{}' is not an existing file", subKey);
            }
        }
        if (auto logDir = SKSE::log::log_directory()) {
            auto candidate = logDir->parent_path() / "SkyrimPrefs.ini";
            if (!std::filesystem::exists(candidate)) {
                logs::warn("Fallback prefs path '{}' does not exist", candidate.string());
            }
            return candidate.string();
        }
        return {};
    }

    std::string ReadIniString(const char* a_key)
    {
        if (g_prefsPath.empty()) {
            return {};
        }
        char buffer[64]{};
        GetPrivateProfileStringA(kSection, a_key, "", buffer, sizeof(buffer), g_prefsPath.c_str());
        return buffer;
    }

    bool WriteIniString(const char* a_key, const std::string& a_value)
    {
        if (g_prefsPath.empty()) {
            return false;
        }
        return WritePrivateProfileStringA(kSection, a_key, a_value.c_str(), g_prefsPath.c_str()) != 0;
    }

    // Prefer the engine's own Setting object when it exists (keeps the
    // in-memory value consistent with the Vanilla menu and lets the engine
    // write the line); otherwise write the INI line directly.
    void WriteFloatPref(const std::string& a_key, float a_value)
    {
        const auto name = a_key + ":" + kSection;
        if (auto* setting = FindPref(name)) {
            setting->data.f = a_value;
            if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton(); prefs && prefs->WriteSetting(setting)) {
                logs::info("Persisted {} = {:.4f} via INIPrefSettingCollection", name, a_value);
                return;
            }
            logs::warn("INIPrefSettingCollection::WriteSetting failed for {}; writing INI line directly", name);
        }
        const auto ok = WriteIniString(a_key.c_str(), std::format("{:.4f}", a_value));
        logs::info("Persisted [{}] {} = {:.4f} to '{}' ({})", kSection, a_key, a_value, g_prefsPath, ok ? "ok" : "FAILED");
    }

    void WriteUIntPref(const std::string& a_key, std::uint32_t a_value)
    {
        const auto name = a_key + ":" + kSection;
        if (auto* setting = FindPref(name)) {
            setting->data.u = a_value;
            if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton(); prefs && prefs->WriteSetting(setting)) {
                logs::info("Persisted {} = {} via INIPrefSettingCollection", name, a_value);
                return;
            }
        }
        const auto ok = WriteIniString(a_key.c_str(), std::to_string(a_value));
        logs::info("Persisted [{}] {} = {} to '{}' ({})", kSection, a_key, a_value, g_prefsPath, ok ? "ok" : "FAILED");
    }

    // ---- plugin-owned INI (Data/SKSE/Plugins/QuickAudioSliders.ini) ----

    std::string ResolvePluginIniPath()
    {
        std::error_code ec;
        auto            path = std::filesystem::absolute(kPluginIniRelative, ec);
        if (ec) {
            logs::warn("Cannot resolve '{}' against the current directory: {}", kPluginIniRelative, ec.message());
            return {};
        }
        return path.string();
    }

    int ReadIniInt(const char* a_section, const char* a_key, int a_default)
    {
        if (g_pluginIniPath.empty()) {
            return a_default;
        }
        return static_cast<int>(GetPrivateProfileIntA(a_section, a_key, a_default, g_pluginIniPath.c_str()));
    }

    // ---- self-documenting INI template ----
    //
    // The INI explains itself: every section and key is written with its
    // default and an English comment. EnsurePluginIni() creates the file from
    // this template and retrofits missing sections/keys into existing files
    // without touching user values or comments.

    struct IniTemplateKey
    {
        std::string_view name;   // bare key name, for the "is it present?" check
        std::string_view block;  // comment line(s) + "Key=default"
    };

    struct IniTemplateSection
    {
        std::string_view      name;      // bare section name without brackets
        std::string_view      block;     // "[Name]" line + section comment
        const IniTemplateKey* keys;      // keys retrofitted individually
        std::size_t           keyCount;
    };

    constexpr auto kIniFileHeader =
        "; Quick Audio Sliders - settings and persistence store.\n"
        "; Created and maintained by the plugin: missing sections and keys are re-added\n"
        "; with their defaults and these comments on startup; existing values and\n"
        "; comments are left untouched.\n"sv;

    constexpr IniTemplateKey kModCategoriesKeys[] = {
        { std::string_view{ QAS::Ini::kModCategoriesEnabledKey },
            "; Enabled: 1 = create the runtime per-mod categories and the \"Extra Sliders\"\n"
            "; page (default), 0 = turn both off. Takes effect on the next game start.\n"
            "Enabled=1"sv },
        { "MinSounds"sv,
            "; MinSounds: a mod only gets a slider when at least this many of its sound\n"
            "; descriptors route through Vanilla categories (default 3).\n"
            "MinSounds=3"sv },
        { "Exclude"sv,
            "; Exclude: comma-separated, case-insensitive plugin-name substrings that\n"
            "; never get a runtime category. Example: Exclude=EVE,Taberu\n"
            "Exclude="sv },
        { "Weather"sv,
            "; Weather: 1 = automatic Rain / Snow / Thunder / Wind sliders for the\n"
            "; weather sounds that no mod slider covers, i.e. those still running through\n"
            "; Vanilla categories such as Ambience (default), 0 = off.\n"
            "Weather=1"sv },
    };

    constexpr IniTemplateSection kIniTemplate[] = {
        { std::string_view{ QAS::Ini::kModCategoriesSection },
            "[ModCategories]\n"
            "; Runtime per-mod sliders (page \"Extra Sliders\") for mods whose sounds only\n"
            "; run through Vanilla sound categories (Effects, Ambience, ...). The plugin\n"
            "; creates one in-memory sound category per mod at startup; nothing is written\n"
            "; to disk or into save games, and mods that already own a menu slider are\n"
            "; skipped. Sound categories a mod defines but hides from the menu (or a\n"
            "; patch hides) still get an Extra slider when sounds use them."sv,
            kModCategoriesKeys, std::size(kModCategoriesKeys) },
        { "Labels"sv,
            "[Labels]\n"
            "; Optional display-name overrides for sliders, shown verbatim.\n"
            "; Key = <plugin file>|<local FormID, 6 hex digits>; the startup log lists\n"
            "; the key of every category. Example (remove the ';' to use it):\n"
            ";Audio Overhaul Skyrim.esp|00AA6E=Waterfalls"sv,
            nullptr, 0 },
        { "Volumes"sv,
            "[Volumes]\n"
            "; Persistence store written by the plugin: the volume of every slider\n"
            "; (<plugin>|<local FormID>, runtime mod categories as\n"
            "; mod:<plugin>|<parent FormID>), GlobalMultiplier, VanillaMaster and\n"
            "; internal markers. Applied on every game start. Managed by the plugin -\n"
            "; do not edit manually."sv,
            nullptr, 0 },
    };

    std::string_view TrimLeft(std::string_view a_text)
    {
        while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) {
            a_text.remove_prefix(1);
        }
        return a_text;
    }

    std::string_view TrimRight(std::string_view a_text)
    {
        while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r')) {
            a_text.remove_suffix(1);
        }
        return a_text;
    }

    bool IsSectionHeader(std::string_view a_line, std::string_view a_name)
    {
        const auto line = TrimRight(TrimLeft(a_line));
        if (line.size() < 2 || line.front() != '[' || line.back() != ']') {
            return false;
        }
        return EqualsNoCase(TrimRight(TrimLeft(line.substr(1, line.size() - 2))), a_name);
    }

    bool IsAnySectionHeader(std::string_view a_line)
    {
        const auto line = TrimLeft(a_line);
        return !line.empty() && line.front() == '[';
    }

    bool IsKeyLine(std::string_view a_line, std::string_view a_key)
    {
        const auto line = TrimLeft(a_line);
        if (line.empty() || line.front() == ';' || line.front() == '[') {
            return false;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            return false;
        }
        return EqualsNoCase(TrimRight(line.substr(0, eq)), a_key);
    }

    void AppendBlockLines(std::vector<std::string>& a_lines, std::size_t a_at, std::string_view a_block)
    {
        std::vector<std::string> block;
        for (std::size_t start = 0; start <= a_block.size();) {
            const auto end = a_block.find('\n', start);
            if (end == std::string_view::npos) {
                block.emplace_back(a_block.substr(start));
                break;
            }
            block.emplace_back(a_block.substr(start, end - start));
            start = end + 1;
        }
        a_lines.insert(a_lines.begin() + static_cast<std::ptrdiff_t>(a_at), block.begin(), block.end());
    }

    // Creates the plugin INI from the template when it does not exist, and
    // appends template sections/keys (with their comments) that are missing
    // from an existing file. Never rewrites lines that are already there, so
    // user values and comments survive.
    void EnsurePluginIni()
    {
        if (g_pluginIniPath.empty()) {
            return;
        }
        std::error_code ec;
        const auto      path = std::filesystem::path(g_pluginIniPath);
        std::filesystem::create_directories(path.parent_path(), ec);

        std::vector<std::string> lines;
        const bool               existed = std::filesystem::exists(path, ec);
        if (existed) {
            std::ifstream in(path);  // text mode: getline strips \r\n
            if (!in) {
                // An existing file that cannot be read (AV/sharing lock,
                // permissions) must not be mistaken for an empty one: the
                // rewrite below would truncate the user's stored values.
                logs::warn("Cannot read '{}'; leaving it untouched", g_pluginIniPath);
                return;
            }
            for (std::string line; std::getline(in, line);) {
                lines.push_back(std::move(line));
            }
        }

        bool changed = false;
        if (lines.empty()) {
            AppendBlockLines(lines, 0, kIniFileHeader);
            changed = true;
        }

        for (const auto& section : kIniTemplate) {
            auto headerIdx = lines.size();
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (IsSectionHeader(lines[i], section.name)) {
                    headerIdx = i;
                    break;
                }
            }
            if (headerIdx == lines.size()) {
                // Whole section missing: append it with every key.
                if (!lines.empty() && !TrimRight(lines.back()).empty()) {
                    lines.emplace_back();
                }
                AppendBlockLines(lines, lines.size(), section.block);
                for (std::size_t k = 0; k < section.keyCount; ++k) {
                    lines.emplace_back();
                    AppendBlockLines(lines, lines.size(), section.keys[k].block);
                }
                changed = true;
                continue;
            }
            // Section exists: find its end and retrofit missing keys there.
            auto sectionEnd = lines.size();
            for (auto i = headerIdx + 1; i < lines.size(); ++i) {
                if (IsAnySectionHeader(lines[i])) {
                    sectionEnd = i;
                    break;
                }
            }
            for (std::size_t k = 0; k < section.keyCount; ++k) {
                const auto& key = section.keys[k];
                bool        found = false;
                for (auto i = headerIdx + 1; i < sectionEnd; ++i) {
                    if (IsKeyLine(lines[i], key.name)) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    continue;
                }
                // Insert before trailing blank lines of the section, with one
                // blank separator line in front of the key's comment block.
                auto insertAt = sectionEnd;
                while (insertAt > headerIdx + 1 && TrimRight(lines[insertAt - 1]).empty()) {
                    --insertAt;
                }
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), std::string{});
                AppendBlockLines(lines, insertAt + 1, key.block);
                changed = true;
                // Recompute the section end after the insertion.
                sectionEnd = lines.size();
                for (auto i = headerIdx + 1; i < lines.size(); ++i) {
                    if (IsAnySectionHeader(lines[i])) {
                        sectionEnd = i;
                        break;
                    }
                }
            }
        }

        if (!changed) {
            return;
        }
        std::ofstream out(path, std::ios::trunc);  // text mode: \n becomes \r\n
        if (!out) {
            logs::warn("Cannot write '{}'", g_pluginIniPath);
            return;
        }
        for (const auto& line : lines) {
            out << line << '\n';
        }
        logs::info("{} '{}' ({} the self-documenting template)", existed ? "Updated" : "Created", g_pluginIniPath,
            existed ? "appended missing parts of" : "wrote");
    }

    // "<defining plugin file>|<local FormID, 6 hex digits>", e.g.
    // "Audio Overhaul Skyrim.esp|00AA6E". Empty when the form has no file.
    std::string PluginIniKey(const QAS::Audio::Entry& a_entry)
    {
        if (a_entry.synthetic) {
            return a_entry.persistKey;
        }
        if (!a_entry.category) {
            return {};
        }
        const auto* file = a_entry.category->GetFile(0);
        if (!file) {
            return {};
        }
        const auto fileName = file->GetFilename();
        if (fileName.empty()) {
            return {};
        }
        return std::format("{}|{:06X}", fileName, a_entry.category->GetLocalFormID());
    }

    std::vector<std::string> g_importedTranslations;  // owner plugins whose translation file was imported

    // Editor ID, FULL and display label of a slider entry whose category,
    // formID and (for synthetic entries) persistKey are set. Imports the
    // owning mod's translation file into our own table before resolving its
    // "$Keys" (SKSE does not import them for every plugin by itself; AOS's
    // "$AOS_Ambience" was missing); a "$Key" that misses is retried on the
    // first render. [Labels] <plugin>|<local FormID> in the plugin INI (same
    // key as the volume store) overrides the label.
    void InitEntryLabel(QAS::Audio::Entry& a_entry)
    {
        auto*       category = a_entry.category;
        const char* editorID = category->GetFormEditorID();
        a_entry.editorID = editorID ? editorID : "";
        if (const auto* owner = OwningModFile(*category)) {
            const std::string ownerName{ owner->GetFilename() };
            if (std::none_of(g_importedTranslations.begin(), g_importedTranslations.end(),
                    [&](const std::string& a_name) { return EqualsNoCase(a_name, ownerName); })) {
                g_importedTranslations.push_back(ownerName);
                QAS::Translation::Import(StripExtension(ownerName));
            }
        }
        const char* fullName = category->GetFullName();
        a_entry.rawFullName = fullName ? fullName : "";
        a_entry.label = QAS::Translation::Translate(fullName && *fullName ? fullName : a_entry.editorID);
        if (a_entry.label.empty()) {
            a_entry.label = std::format("{:08X}", a_entry.formID);
        }
        a_entry.labelPendingTranslation =
            a_entry.rawFullName.starts_with('$') && a_entry.label == a_entry.rawFullName.substr(1);
        if (a_entry.rawFullName.starts_with('$')) {
            logs::info("Label for {:08X}: FULL '{}' -> '{}'{}", a_entry.formID, a_entry.rawFullName, a_entry.label,
                a_entry.labelPendingTranslation ? " (no translation found yet)" : "");
        }
        if (const auto key = PluginIniKey(a_entry); !key.empty() && !g_pluginIniPath.empty()) {
            char buffer[128]{};
            GetPrivateProfileStringA(kLabelsSection, key.c_str(), "", buffer, sizeof(buffer), g_pluginIniPath.c_str());
            if (buffer[0] != '\0') {
                a_entry.label = buffer;
                a_entry.labelOverridden = true;
                logs::info("Label override for {}: '{}'", key, a_entry.label);
            }
        }
    }

    // ---- volume store ----
    //
    // [Volumes] of the plugin INI is the source of truth for every slider,
    // the global multiplier and the Vanilla master: values are written on
    // every change and applied at kDataLoaded, after the engine and other
    // plugins restored theirs. Every write is read back. A plugin INI that
    // does not take the value (read-only file, a mod manager's virtual file
    // system, antivirus) is replaced by the fallback file next to the SKSE
    // logs, key by key: a key present in the fallback is newer than the same
    // key in the plugin INI, and is removed from the fallback as soon as the
    // plugin INI accepts a write for it again.

    std::string ReadStoreString(const std::string& a_path, const std::string& a_key)
    {
        if (a_path.empty() || a_key.empty()) {
            return {};
        }
        char buffer[32]{};
        GetPrivateProfileStringA(kVolumesSection, a_key.c_str(), "", buffer, sizeof(buffer), a_path.c_str());
        return buffer;
    }

    std::optional<float> ParseStoreVolume(const std::string& a_text, const std::string& a_key, const std::string& a_path)
    {
        if (a_text.empty()) {
            return std::nullopt;
        }
        // std::from_chars, not strtof: strtof parses through the C locale,
        // which another plugin in the process may have changed (a "0.9500"
        // read as 0.0 would silently mute everything restored).
        float      value = 0.0F;
        const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
        if (result.ec != std::errc{} || !std::isfinite(value)) {
            logs::warn("[{}] {} = '{}' in '{}' is not a number; ignored", kVolumesSection, a_key, a_text, a_path);
            return std::nullopt;
        }
        return Clamp01(value);
    }

    std::optional<float> ReadPluginVolume(const std::string& a_key)
    {
        if (g_fallbackActive) {
            if (const auto value = ParseStoreVolume(ReadStoreString(g_fallbackIniPath, a_key), a_key, g_fallbackIniPath)) {
                return value;
            }
        }
        return ParseStoreVolume(ReadStoreString(g_pluginIniPath, a_key), a_key, g_pluginIniPath);
    }

    // Writes one [Volumes] line and reads it back; false when the file did
    // not take the value.
    bool WriteStoreString(const std::string& a_path, const std::string& a_key, const std::string& a_text)
    {
        if (a_path.empty() || a_key.empty()) {
            return false;
        }
        const bool          written = WritePrivateProfileStringA(kVolumesSection, a_key.c_str(), a_text.c_str(), a_path.c_str()) != 0;
        const unsigned long error = written ? 0UL : static_cast<unsigned long>(GetLastError());
        if (written && ReadStoreString(a_path, a_key) == a_text) {
            return true;
        }
        logs::warn("[{}] {} = {} was not stored in '{}' ({})", kVolumesSection, a_key, a_text, a_path,
            written ? "the value read back differs" : std::format("write failed, Windows error {}", error));
        return false;
    }

    bool WritePluginVolume(const std::string& a_key, float a_volume)
    {
        if (a_key.empty()) {
            return false;
        }
        const auto text = std::format("{:.4f}", a_volume);
        if (!g_pluginIniPath.empty()) {
            std::error_code ec;
            if (!std::filesystem::exists(std::filesystem::path(g_pluginIniPath), ec)) {
                // First write without a file (deleted since startup): recreate the
                // commented template so the key lands in a self-documenting file.
                EnsurePluginIni();
            }
            if (WriteStoreString(g_pluginIniPath, a_key, text)) {
                if (g_fallbackActive) {
                    // The plugin INI holds the newest value again.
                    WritePrivateProfileStringA(kVolumesSection, a_key.c_str(), nullptr, g_fallbackIniPath.c_str());
                }
                return true;
            }
        }
        if (g_fallbackIniPath.empty()) {
            return false;
        }
        if (!g_fallbackWarned) {
            g_fallbackWarned = true;
            logs::warn("The plugin INI '{}' does not accept values; storing them in '{}' instead", g_pluginIniPath,
                g_fallbackIniPath);
        }
        if (WriteStoreString(g_fallbackIniPath, a_key, text)) {
            g_fallbackActive = true;
            return true;
        }
        return false;
    }

    // Number of "key=value" lines in [Volumes] of a store file, for the
    // startup log.
    std::size_t CountStoreValues(const std::string& a_path)
    {
        if (a_path.empty()) {
            return 0;
        }
        std::vector<char> buffer(32767, '\0');
        const auto        length = GetPrivateProfileSectionA(kVolumesSection, buffer.data(), static_cast<unsigned long>(buffer.size()),
            a_path.c_str());
        std::size_t count = 0;
        for (std::size_t i = 0; i < length;) {
            const std::string_view line{ buffer.data() + i };
            if (line.empty()) {
                break;
            }
            ++count;
            i += line.size() + 1;
        }
        return count;
    }

    // The engine registers uID0..uID7 as settings and leaves unused slots at
    // 0, so a FormID of 0 means "free", not "assigned".
    std::optional<std::uint32_t> ReadSlotFormID(int a_slot)
    {
        const auto    key = std::format("uID{}", a_slot);
        std::uint32_t value = 0;
        if (auto* setting = FindPref(key + ":" + kSection)) {
            value = setting->data.u;
        } else {
            const auto text = ReadIniString(key.c_str());
            if (text.empty()) {
                return std::nullopt;
            }
            char* end = nullptr;
            value = static_cast<std::uint32_t>(std::strtoul(text.c_str(), &end, 10));
            if (end == text.c_str()) {
                return std::nullopt;
            }
        }
        return value != 0 ? std::optional(value) : std::nullopt;
    }

    // Setting a volume only changes the stored multiplier; sounds that are
    // already playing keep their output level until the audio thread
    // re-evaluates them. Papyrus' SoundCategory.SetVolume does exactly this
    // after SetCategoryVolume (SkyrimSE 1.6.1170: SetCategoryVolume, then
    // BSAudioManager message 0x10 = SOUND_MSG::VolumeChanged), so we do too.
    void BroadcastVolumeChanged()
    {
        if (auto* audio = RE::BSAudioManager::GetSingleton()) {
            audio->ComposeMessage(RE::SOUND_MSG::VolumeChanged);
        }
    }

    // The Vanilla master is not a slider here any more, but builds <= 9 could
    // leave it at 0 (and so can the Vanilla menu). Restoring it to 100% is
    // part of "reset all", and done once at startup when it is found at 0,
    // so nobody stays in a silent game without a visible reason.
    void ApplyVanillaMaster(float a_value)
    {
        if (auto* setting = FindPref(kMasterSetting)) {
            setting->data.f = a_value;
        }
        if (auto* audio = RE::BSAudioManager::GetSingleton()) {
            audio->masterVolume = a_value;
        }
        BroadcastVolumeChanged();
        WriteFloatPref("fAudioMasterVolume", a_value);
    }

    void RestoreVanillaMaster(const char* a_reason)
    {
        ApplyVanillaMaster(1.0F);
        if (WritePluginVolume(kVanillaMasterKey, 1.0F)) {
            g_savedMaster = 1.0F;
        }
        logs::info("Vanilla master volume restored to 100% ({})", a_reason);
    }

    // ---- runtime per-mod categories ([ModCategories] in the plugin INI) ----

    struct ModCategoryPlan
    {
        std::string                             pluginFile;  // source plugin of the descriptors
        RE::BGSSoundCategory*                   parent{ nullptr };
        std::vector<RE::BGSSoundDescriptor*>    descriptors;
        RE::BGSSoundCategory*                   created{ nullptr };
        // Weather plans (PlanWeatherCategories) fix both; empty = derived
        // from the mod name and the parent.
        std::string                             label;
        std::string                             persistKey;
    };
    std::vector<ModCategoryPlan> g_modPlans;

    // Engine-managed Vanilla categories a descriptor must keep pointing at
    // directly: pausing/ducking/underwater muting is keyed on them.
    bool IsEngineManagedVanillaCategory(const RE::BGSSoundCategory& a_category)
    {
        const auto* files = a_category.sourceFiles.array;
        if (!files || files->empty() || !files->front() || !IsVanillaFile(files->front()->GetFilename())) {
            return false;
        }
        switch (a_category.GetFormID() & 0x00FFFFFF) {
        case 0x10AA60:  // AudioCategoryPausedDuringMenuLoad
        case 0x0DDDC5:  // AudioCategoryPausedDuringMenu
        case 0x09F254:  // AudioCategoryPausedDuringMenuFade
        case 0x000E9E:  // AudioCategorySFXFadeDuringDialogue
        case 0x0F23BB:  // AudioCategoryMuteSubmerged
            return true;
        default:
            break;
        }
        const char* fullName = a_category.GetFullName();
        return fullName && IsEngineManagedCategory(fullName);
    }

    // Text of every Sound Record Distributor config that mentions a category
    // assignment, lower-cased; a descriptor whose plugin and FormID both
    // appear in such a file is left to SRD.
    std::vector<std::string> LoadSrdCategoryConfigs()
    {
        std::vector<std::string> texts;
        std::error_code          ec;
        const auto               folder = std::filesystem::absolute(kSrdFolderRelative, ec);
        if (ec || !std::filesystem::exists(folder, ec)) {
            return texts;
        }
        for (const auto& item : std::filesystem::recursive_directory_iterator(folder, ec)) {
            if (!item.is_regular_file(ec)) {
                continue;
            }
            const auto ext = LowerCopy(item.path().extension().string());
            if (ext != ".json" && ext != ".ini" && ext != ".toml" && ext != ".yaml" && ext != ".yml") {
                continue;
            }
            std::ifstream stream(item.path(), std::ios::binary);
            std::string   text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            text = LowerCopy(text);
            if (text.find("category") != std::string::npos) {
                texts.push_back(std::move(text));
            }
        }
        return texts;
    }

    bool SrdAssignsCategory(const std::vector<std::string>& a_configs, std::string_view a_pluginFile, RE::FormID a_localID)
    {
        if (a_configs.empty()) {
            return false;
        }
        const auto plugin = LowerCopy(a_pluginFile);
        const auto idLong = LowerCopy(std::format("{:06x}", a_localID));
        const auto idShort = LowerCopy(std::format("{:x}", a_localID));
        for (const auto& text : a_configs) {
            if (text.find(plugin) != std::string::npos &&
                (text.find(idLong) != std::string::npos || text.find("0x" + idShort) != std::string::npos)) {
                return true;
            }
        }
        return false;
    }

    // Friendly name of a parent category for "<mod> (<parent>)" labels: the
    // menu slider's label when the parent is one, else a readable name for
    // the Vanilla routing nodes, else the FULL without "AudioCategory".
    std::string ParentLabel(const RE::BGSSoundCategory& a_parent)
    {
        for (const auto& entry : g_entries) {
            if (entry->category == &a_parent && !entry->synthetic) {
                auto label = entry->label;
                if (const auto* owner = OwningModFile(a_parent)) {
                    label = StripModPrefix(label, owner->GetFilename());
                }
                return TitleCaseIfAllCaps(label);
            }
        }
        switch (a_parent.GetFormID() & 0x00FFFFFF) {
        case 0x07F80B:
            return "Ambience";  // AudioCategoryAMB has no FULL; the raw name read as "AMB"
        case 0x071E63:
            return "Ambience (region)";
        case 0x0EA51F:
            return "Kill moves";
        case 0x0876BC:
            return "Non-voice";
        case 0x0876BB:
            return "Dialogue voice";
        default:
            break;
        }
        const char* fullName = a_parent.GetFullName();
        auto        label = QAS::Translation::Translate(fullName ? fullName : "");
        if (StartsWithNoCase(label, "AudioCategory"sv)) {
            label.erase(0, std::string_view("AudioCategory").size());
        }
        return TitleCaseIfAllCaps(label);
    }

    std::vector<std::string> ReadExcludeList()
    {
        std::vector<std::string> result;
        if (g_pluginIniPath.empty()) {
            return result;
        }
        char buffer[2048]{};
        GetPrivateProfileStringA(kModCategoriesSection, "Exclude", "", buffer, sizeof(buffer), g_pluginIniPath.c_str());
        std::string_view text{ buffer };
        while (!text.empty()) {
            const auto comma = text.find(',');
            const auto item = Trim(std::string(text.substr(0, comma)));
            if (!item.empty()) {
                result.push_back(LowerCopy(item));
            }
            if (comma == std::string_view::npos) {
                break;
            }
            text.remove_prefix(comma + 1);
        }
        return result;
    }

    // a_excludes: lower-cased patterns from ReadExcludeList().
    bool IsExcluded(const std::vector<std::string>& a_excludes, std::string_view a_plugin)
    {
        const auto lower = LowerCopy(a_plugin);
        return std::any_of(a_excludes.begin(), a_excludes.end(),
            [&](const std::string& a_pattern) { return lower.find(a_pattern) != std::string::npos; });
    }

    // Plans one runtime category per (mod plugin, original parent category)
    // for descriptors from non-official plugins that hang on Vanilla-defined
    // categories, when the mod has at least a_minSounds such descriptors.
    // Mods that already own a menu slider (AOS, ISC, mods with their own
    // SNCT) are skipped entirely: their sounds are already adjustable and a
    // runtime copy would only duplicate the Volume page.
    void PlanModCategories(RE::TESDataHandler* a_dataHandler, int a_minSounds)
    {
        g_modPlans.clear();
        const auto srdConfigs = LoadSrdCategoryConfigs();
        if (!srdConfigs.empty()) {
            logs::info("SoundRecordDistributor: {} config(s) mention categories; matching descriptors are skipped",
                srdConfigs.size());
        }
        const auto excludes = ReadExcludeList();
        if (!excludes.empty()) {
            logs::info("[ModCategories] Exclude: {} pattern(s)", excludes.size());
        }

        // Plugins that already have a slider on the Volume page.
        std::vector<std::string> ownersWithSliders;
        for (const auto& entry : g_entries) {
            if (entry->synthetic || !entry->category) {
                continue;
            }
            if (const auto* owner = OwningModFile(*entry->category)) {
                ownersWithSliders.emplace_back(owner->GetFilename());
            }
        }
        auto ownsSlider = [&](std::string_view a_plugin) {
            return std::any_of(ownersWithSliders.begin(), ownersWithSliders.end(),
                [&](const std::string& a_owner) { return EqualsNoCase(a_owner, a_plugin); });
        };
        auto excluded = [&](std::string_view a_plugin) { return IsExcluded(excludes, a_plugin); };

        std::size_t skippedEngine = 0, skippedSrd = 0, skippedModOwned = 0, skippedHasSlider = 0, skippedExcluded = 0;
        for (auto* form : a_dataHandler->GetFormArray<RE::BGSSoundDescriptorForm>()) {
            if (!form || !form->soundDescriptor) {
                continue;
            }
            auto* descriptor = form->soundDescriptor;
            auto* category = descriptor->category;
            if (!category) {
                continue;
            }
            const auto* sourceFiles = form->sourceFiles.array;
            const auto* source = (sourceFiles && !sourceFiles->empty()) ? sourceFiles->front() : nullptr;
            if (!source || IsVanillaFile(source->GetFilename())) {
                continue;  // Vanilla descriptor
            }
            if (ownsSlider(source->GetFilename())) {
                ++skippedHasSlider;
                continue;  // the mod already has its own slider(s) on the Volume page
            }
            if (excluded(source->GetFilename())) {
                ++skippedExcluded;
                continue;
            }
            const auto* categoryFiles = category->sourceFiles.array;
            const auto* categoryDefiner = (categoryFiles && !categoryFiles->empty()) ? categoryFiles->front() : nullptr;
            if (!categoryDefiner || !IsVanillaFile(categoryDefiner->GetFilename())) {
                ++skippedModOwned;
                continue;  // the mod (or another mod) already owns this category
            }
            if (IsEngineManagedVanillaCategory(*category)) {
                ++skippedEngine;
                continue;
            }
            const std::string pluginFile{ source->GetFilename() };
            if (SrdAssignsCategory(srdConfigs, pluginFile, form->GetLocalFormID())) {
                ++skippedSrd;
                continue;
            }
            auto it = std::find_if(g_modPlans.begin(), g_modPlans.end(), [&](const ModCategoryPlan& a_plan) {
                return a_plan.parent == category && EqualsNoCase(a_plan.pluginFile, pluginFile);
            });
            if (it == g_modPlans.end()) {
                g_modPlans.push_back(ModCategoryPlan{ pluginFile, category, {}, nullptr });
                it = std::prev(g_modPlans.end());
            }
            it->descriptors.push_back(descriptor);
        }

        // Minimum applies per mod (all parents together).
        std::vector<std::string> keep;
        for (const auto& plan : g_modPlans) {
            std::size_t total = 0;
            for (const auto& other : g_modPlans) {
                if (EqualsNoCase(other.pluginFile, plan.pluginFile)) {
                    total += other.descriptors.size();
                }
            }
            if (total >= static_cast<std::size_t>(std::max(a_minSounds, 1))) {
                keep.push_back(plan.pluginFile);
            }
        }
        std::erase_if(g_modPlans, [&](const ModCategoryPlan& a_plan) {
            return std::none_of(keep.begin(), keep.end(),
                [&](const std::string& a_file) { return EqualsNoCase(a_file, a_plan.pluginFile); });
        });
        // A mod with several parents: drop one-descriptor sub-plans (they
        // would be a slider for a single sound); those sounds stay on the
        // Vanilla slider.
        std::erase_if(g_modPlans, [&](const ModCategoryPlan& a_plan) {
            if (a_plan.descriptors.size() >= 2) {
                return false;
            }
            return std::any_of(g_modPlans.begin(), g_modPlans.end(), [&](const ModCategoryPlan& a_other) {
                return &a_other != &a_plan && EqualsNoCase(a_other.pluginFile, a_plan.pluginFile);
            });
        });
        logs::info("Mod categories: {} plan(s) (skipped descriptors: {} of mods with own slider, {} excluded, "
                   "{} engine-managed, {} SRD-assigned, {} on mod-owned categories)",
            g_modPlans.size(), skippedHasSlider, skippedExcluded, skippedEngine, skippedSrd, skippedModOwned);
    }

    // Sound categories a mod defines without the "Should appear on menu"
    // flag, or whose flag a patch removed (VSfW's Cathedral patch hides
    // Cathedral Weathers' Rain / Thunder / Interior Rain): sounds hang on
    // them, so the runtime per-mod categories above skip them as
    // "mod-owned", yet no menu shows a slider for them. They get an
    // "Extra Sliders" entry that drives the existing category directly; no
    // descriptor is re-pointed. Needed: at least one descriptor on the
    // category itself, not engine-managed, plugin not excluded. MinSounds
    // does not apply: it keeps runtime categories from splitting off stray
    // sounds, but a category the mod made on purpose can carry a single
    // descriptor (Cathedral Weathers Unofficial Update leaves exactly one,
    // an interior-only, conditioned rain loop, on the hidden "Interior
    // Rain"; it is not the main rain, see docs/design-notes.md).
    void AddUnlistedModCategories(RE::TESDataHandler* a_dataHandler)
    {
        std::unordered_map<RE::BGSSoundCategory*, std::size_t> descriptorCounts;
        for (auto* form : a_dataHandler->GetFormArray<RE::BGSSoundDescriptorForm>()) {
            if (form && form->soundDescriptor && form->soundDescriptor->category) {
                ++descriptorCounts[form->soundDescriptor->category];
            }
        }
        const auto excludes = ReadExcludeList();

        std::size_t added = 0;
        for (auto* category : a_dataHandler->GetFormArray<RE::BGSSoundCategory>()) {
            if (!category || category->IsMenuCategory()) {
                continue;
            }
            const auto* files = category->sourceFiles.array;
            const auto* definer = (files && !files->empty()) ? files->front() : nullptr;
            if (!definer || IsVanillaFile(definer->GetFilename())) {
                continue;
            }
            const std::string pluginFile{ definer->GetFilename() };
            const char*       editorID = category->GetFormEditorID();
            const char*       fullName = category->GetFullName();
            if (IsEngineManagedCategory(editorID ? editorID : ""sv) || IsEngineManagedCategory(fullName ? fullName : ""sv)) {
                continue;
            }
            const auto count = descriptorCounts[category];
            if (count == 0) {
                continue;  // unused, or only a parent of other categories
            }
            if (IsExcluded(excludes, pluginFile)) {
                continue;
            }

            auto entry = std::make_unique<QAS::Audio::Entry>();
            entry->category = category;
            entry->formID = category->GetFormID();
            entry->synthetic = true;  // "Extra Sliders" page, persisted in the plugin INI
            entry->ownerFileName = pluginFile;
            entry->persistKey = std::format("{}|{:06X}", pluginFile, category->GetLocalFormID());
            InitEntryLabel(*entry);
            logs::info("Unlisted mod category {:08X} '{}' of '{}' ({} descriptor(s), no menu flag): Extra slider, key '{}'",
                entry->formID, entry->label, pluginFile, count, entry->persistKey);
            g_entries.push_back(std::move(entry));
            ++added;
        }
        logs::info("Unlisted mod categories: {} Extra slider(s)", added);
    }

    // Creates the dynamic BGSSoundCategory forms (0xFF FormIDs, never saved)
    // and the matching slider entries. Descriptors are re-pointed later, one
    // frame after kDataLoaded (see Initialize), so other plugins' kDataLoaded
    // work has finished.
    constexpr auto kWeatherOwner = "Weather"sv;  // pseudo plugin name: group heading of the weather sliders

    // "defined in 'A.esp'" or "defined in 'A.esp', last changed by 'B.esp'":
    // the plugins a record comes from, for log lines about single sounds
    // (editor IDs are not kept at runtime).
    std::string SourceFilesText(const RE::TESForm& a_form)
    {
        const auto* files = a_form.sourceFiles.array;
        if (!files || files->empty() || !files->front()) {
            return "no source file";
        }
        auto text = std::format("defined in '{}'", files->front()->GetFilename());
        if (files->size() > 1 && files->back() && files->back() != files->front()) {
            text += std::format(", last changed by '{}'", files->back()->GetFilename());
        }
        return text;
    }

    // Automatic weather sliders. Every WTHR lists its sounds with a type
    // (precipitation / wind / thunder). A listed sound that still hangs on a
    // Vanilla-defined category (Ambience, ...) has no slider of its own: it
    // gets a runtime category per type under that parent, exactly like the
    // per-mod categories. A sound a mod already put on its own category
    // (NAT, True Storms, Volume Sliders for Weather and its patches) is left
    // alone, so no load-order rules are needed; when such a mod slider exists
    // for a type, ours is labelled "... other" to tell the two apart.
    // Precipitation is Rain or Snow by the flags of the weathers using it.
    // Not found this way: the Vanilla interior rain loop, which no record
    // references (the engine plays it).
    void PlanWeatherCategories(RE::TESDataHandler* a_dataHandler)
    {
        enum Kind : std::size_t { kRain, kSnow, kThunder, kWind, kKinds };
        static constexpr std::array<std::string_view, kKinds> kNames{ "Rain"sv, "Snow"sv, "Thunder"sv, "Wind"sv };

        struct Usage
        {
            RE::BGSSoundDescriptorForm*    form{ nullptr };
            std::array<std::size_t, kKinds> votes{};
        };
        std::unordered_map<RE::BGSSoundDescriptor*, Usage> usages;
        std::size_t                                         weathers = 0;
        for (auto* weather : a_dataHandler->GetFormArray<RE::TESWeather>()) {
            if (!weather) {
                continue;
            }
            ++weathers;
            const bool snowy = weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kSnow) &&
                               !weather->data.flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
            for (auto* sound : weather->sounds) {
                if (!sound) {
                    continue;
                }
                Kind kind = kKinds;
                switch (sound->type.get()) {
                case RE::TESWeather::SoundType::kPrecip:
                    kind = snowy ? kSnow : kRain;
                    break;
                case RE::TESWeather::SoundType::kThunder:
                    kind = kThunder;
                    break;
                case RE::TESWeather::SoundType::kWind:
                    kind = kWind;
                    break;
                default:
                    break;  // "Default" entries are general ambience, not weather noise
                }
                if (kind == kKinds) {
                    continue;
                }
                // The entry names a sound descriptor, or a SOUN wrapping one.
                auto* form = RE::TESForm::LookupByID(sound->soundFormID);
                auto* descriptorForm = form ? form->As<RE::BGSSoundDescriptorForm>() : nullptr;
                if (!descriptorForm && form) {
                    if (const auto* soun = form->As<RE::TESSound>()) {
                        descriptorForm = soun->descriptor;
                    }
                }
                if (!descriptorForm || !descriptorForm->soundDescriptor) {
                    continue;
                }
                auto& usage = usages[descriptorForm->soundDescriptor];
                usage.form = descriptorForm;
                ++usage.votes[kind];
            }
        }

        const auto srdConfigs = LoadSrdCategoryConfigs();
        const auto excludes = ReadExcludeList();
        std::array<bool, kKinds> coveredByMod{};
        std::size_t              onModCategory = 0, engineManaged = 0, skipped = 0, moved = 0;
        std::vector<ModCategoryPlan>                          plans;
        std::vector<Kind>                                     planKinds;
        std::vector<std::vector<RE::BGSSoundDescriptorForm*>> planForms;  // for the log
        for (auto& [descriptor, usage] : usages) {
            const auto kind = static_cast<Kind>(
                std::distance(usage.votes.begin(), std::max_element(usage.votes.begin(), usage.votes.end())));
            auto* category = descriptor->category;
            if (!category) {
                ++skipped;
                continue;
            }
            const auto* categoryFiles = category->sourceFiles.array;
            const auto* categoryDefiner = (categoryFiles && !categoryFiles->empty()) ? categoryFiles->front() : nullptr;
            if (!categoryDefiner || !IsVanillaFile(categoryDefiner->GetFilename())) {
                coveredByMod[kind] = true;  // a mod routes this sound through its own category
                ++onModCategory;
                continue;
            }
            if (IsEngineManagedVanillaCategory(*category)) {
                ++engineManaged;
                continue;
            }
            const auto* sourceFiles = usage.form->sourceFiles.array;
            const auto* source = (sourceFiles && !sourceFiles->empty()) ? sourceFiles->front() : nullptr;
            const std::string pluginFile{ source ? source->GetFilename() : std::string_view{} };
            if (IsExcluded(excludes, pluginFile) ||
                SrdAssignsCategory(srdConfigs, pluginFile, usage.form->GetLocalFormID())) {
                ++skipped;
                continue;
            }
            std::size_t planIndex = 0;
            for (; planIndex < plans.size(); ++planIndex) {
                if (planKinds[planIndex] == kind && plans[planIndex].parent == category) {
                    break;
                }
            }
            if (planIndex == plans.size()) {
                ModCategoryPlan plan;
                plan.pluginFile = std::string(kWeatherOwner);
                plan.parent = category;
                // GetLocalFormID: the parent is Vanilla-defined, so the key
                // does not depend on the load order.
                plan.persistKey = std::format("weather:{}|{:08X}", kNames[kind], category->GetLocalFormID());
                plans.push_back(std::move(plan));
                planKinds.push_back(kind);
                planForms.emplace_back();
            }
            plans[planIndex].descriptors.push_back(descriptor);
            planForms[planIndex].push_back(usage.form);
        }

        // Where a mod already has a slider for the type, a single left-over
        // sound is not worth a second one (lab, 2026-11-24: every weather mod
        // left exactly one thunder descriptor behind): it stays where it is.
        std::size_t leftAlone = 0;
        for (std::size_t index = plans.size(); index-- > 0;) {
            if (coveredByMod[planKinds[index]] && plans[index].descriptors.size() < 2) {
                for (const auto* form : planForms[index]) {
                    logs::info("Weather sound {:08X} ({}) stays on its Vanilla slider: the only one a mod's {} slider "
                               "does not cover",
                        form->GetFormID(), SourceFilesText(*form), kNames[planKinds[index]]);
                }
                leftAlone += plans[index].descriptors.size();
                plans.erase(plans.begin() + static_cast<std::ptrdiff_t>(index));
                planKinds.erase(planKinds.begin() + static_cast<std::ptrdiff_t>(index));
                planForms.erase(planForms.begin() + static_cast<std::ptrdiff_t>(index));
            }
        }
        std::vector<const RE::BGSSoundDescriptor*> claimed;
        for (const auto& plan : plans) {
            claimed.insert(claimed.end(), plan.descriptors.begin(), plan.descriptors.end());
            moved += plan.descriptors.size();
        }

        // A weather sound of a mod without own slider was planned into that
        // mod's "<Mod> (Ambience)" category: the weather slider takes it.
        for (auto& plan : g_modPlans) {
            std::erase_if(plan.descriptors, [&](const RE::BGSSoundDescriptor* a_descriptor) {
                return std::find(claimed.begin(), claimed.end(), a_descriptor) != claimed.end();
            });
        }
        std::erase_if(g_modPlans, [](const ModCategoryPlan& a_plan) { return a_plan.descriptors.empty(); });

        for (std::size_t index = 0; index < plans.size(); ++index) {
            const auto kind = planKinds[index];
            const auto sameKind = static_cast<std::size_t>(std::count(planKinds.begin(), planKinds.end(), kind));
            std::string detail = coveredByMod[kind] ? "weather, other" : "weather";
            if (sameKind > 1) {
                if (const auto parentLabel = ParentLabel(*plans[index].parent); !parentLabel.empty()) {
                    detail += ", " + parentLabel;
                }
            }
            plans[index].label = std::format("{} ({})", kNames[kind], detail);
            for (const auto* form : planForms[index]) {
                logs::info("Weather slider '{}': sound {:08X} ({})", plans[index].label, form->GetFormID(),
                    SourceFilesText(*form));
            }
            g_modPlans.push_back(std::move(plans[index]));
        }
        logs::info("Weather sounds: {} descriptor(s) listed by {} weather(s) as precipitation / thunder / wind: "
                   "{} on a mod's own category, {} engine-managed, {} excluded or without category, "
                   "{} single left-over(s) left alone, {} get {} weather slider(s)",
            usages.size(), weathers, onModCategory, engineManaged, skipped, leftAlone, moved, plans.size());
    }

    void CreateModCategories()
    {
        auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSSoundCategory>();
        if (!factory) {
            logs::error("No form factory for BGSSoundCategory; mod categories disabled");
            g_modPlans.clear();
            return;
        }
        for (auto& plan : g_modPlans) {
            auto* created = factory->Create();
            if (!created) {
                logs::error("Failed to create a sound category for '{}'", plan.pluginFile);
                continue;
            }
            std::size_t sameMod = 0;
            for (const auto& other : g_modPlans) {
                sameMod += EqualsNoCase(other.pluginFile, plan.pluginFile) ? 1 : 0;
            }
            const auto parentLabel = ParentLabel(*plan.parent);
            auto       label = ShortModName(plan.pluginFile);
            if (!plan.label.empty()) {
                label = plan.label;
            } else if (sameMod > 1 && !parentLabel.empty()) {
                label = std::format("{} ({})", label, parentLabel);
            }

            created->fullName = label.c_str();
            created->parentCategory = plan.parent;
            created->flags.reset(RE::BGSSoundCategory::Flag::kShouldAppearOnMenu);  // never: 0xFF IDs must stay out of uID slots
            if (plan.parent->flags.any(RE::BGSSoundCategory::Flag::kMuteWhenSubmerged)) {
                created->flags.set(RE::BGSSoundCategory::Flag::kMuteWhenSubmerged);
            }
            created->staticMult = 0xFFFF;
            created->defaultMenuValue = 0xFFFF;
            created->volumeMult = 1.0F;
            created->frequencyMult = 1.0F;
            created->attenuation = 0;
            plan.created = created;

            auto entry = std::make_unique<QAS::Audio::Entry>();
            entry->category = created;
            entry->formID = created->GetFormID();
            entry->editorID = std::format("(runtime: {})", plan.pluginFile);
            entry->label = label;
            entry->labelOverridden = true;  // already tidy; keep the mod name as is
            entry->synthetic = true;
            entry->ownerFileName = plan.pluginFile;
            // GetLocalFormID, not a mask: an ESL-defined parent's FE-prefixed
            // slot is load-order-dependent and must stay out of the key.
            entry->persistKey = !plan.persistKey.empty() ?
                                    plan.persistKey :
                                    std::format("mod:{}|{:08X}", plan.pluginFile, plan.parent->GetLocalFormID());
            logs::info("Mod category '{}' {:08X} for {} descriptor(s) of '{}' (parent {:08X} '{}', key '{}')", label,
                entry->formID, plan.descriptors.size(), plan.pluginFile, plan.parent->GetFormID(), parentLabel,
                entry->persistKey);
            g_entries.push_back(std::move(entry));
        }
    }

    // Game thread, one frame after kDataLoaded.
    void RepointModDescriptors()
    {
        std::size_t repointed = 0;
        for (const auto& plan : g_modPlans) {
            if (!plan.created) {
                continue;
            }
            for (auto* descriptor : plan.descriptors) {
                if (descriptor->category == plan.parent) {  // untouched since planning
                    descriptor->category = plan.created;
                    ++repointed;
                }
            }
        }
        if (!g_modPlans.empty()) {
            logs::info("{} sound descriptor(s) re-pointed to runtime mod categories", repointed);
        }
    }

    RootCategory* FindRoot(const RE::BGSSoundCategory* a_category)
    {
        for (auto& root : g_roots) {
            if (root.category == a_category) {
                return &root;
            }
        }
        return nullptr;
    }

    void ApplyNow(QAS::Audio::Entry& a_entry, float a_volume)
    {
        // A menu category that is also a root (SkyParkour, Word Walls)
        // keeps its own slider value: while the global multiplier is
        // active the slider sets the root's baseline and the engine sees
        // baseline × global, so the slider never shows the global value.
        if (auto* root = FindRoot(a_entry.category); root && g_globalActive.load(std::memory_order_relaxed)) {
            root->baseline.store(a_volume, std::memory_order_relaxed);
            a_entry.category->SetCategoryVolume(Clamp01(a_volume * g_global.load()));
        } else {
            a_entry.category->SetCategoryVolume(a_volume);
        }
        BroadcastVolumeChanged();
    }

    bool HasEngineSlot(const QAS::Audio::Entry& a_entry)
    {
        return !a_entry.synthetic && a_entry.slot >= 0 && a_entry.slot < g_engineSlots;
    }

    // Game thread. A menu slider (or the Vanilla master) that no longer has
    // the value the plugin stored was changed somewhere else, in practice in
    // Settings > Audio: take the new value into the store, otherwise the next
    // start would put the old one back. Called when the Journal Menu closes.
    // "Extra Sliders" are left out: nothing but this plugin offers them, and a
    // mod may duck its own hidden category temporarily.
    void AdoptExternalChanges()
    {
        if (!g_ready.load()) {
            return;
        }
        std::size_t adopted = 0;
        for (auto& entryPtr : g_entries) {
            auto& entry = *entryPtr;
            if (entry.synthetic || entry.pending.load() || entry.dirty.load()) {
                continue;
            }
            float current = QAS::Audio::ReadVolume(entry);
            // A root that is also a menu slider holds baseline x global while
            // the multiplier is active, and ReadVolume() answers with the
            // baseline. The Vanilla menu sets volumeMult directly: when it no
            // longer equals baseline x global, the value divided by the
            // multiplier is the new baseline. Not while the global slider is
            // being dragged: g_global is then ahead of what was applied.
            if (auto* root = FindRoot(entry.category);
                root && g_globalActive.load(std::memory_order_relaxed) && !g_globalDirty.load()) {
                const float global = g_global.load();
                const float actual = Clamp01(entry.category->volumeMult);
                const float baseline = root->baseline.load(std::memory_order_relaxed);
                if (global > kVolumeEpsilon && std::abs(actual - Clamp01(baseline * global)) > kVolumeEpsilon) {
                    current = Clamp01(actual / global);
                    root->baseline.store(current, std::memory_order_relaxed);
                }
            }
            if (std::abs(current - entry.savedVolume) <= kVolumeEpsilon) {
                continue;
            }
            const auto key = PluginIniKey(entry);
            if (key.empty()) {
                continue;
            }
            const bool ok = WritePluginVolume(key, current);
            logs::info("Adopted {:08X} '{}' key '{}': {:.4f} -> {:.4f}, changed outside this plugin ({})", entry.formID,
                entry.label, key, entry.savedVolume, current, ok ? "stored" : "NOT STORED");
            entry.savedVolume = current;
            entry.uiVolume = current;
            ++adopted;
        }
        if (const auto* setting = FindPref(kMasterSetting)) {
            const float master = Clamp01(setting->data.f);
            if (g_savedMaster < 0.0F || std::abs(master - g_savedMaster) > kVolumeEpsilon) {
                const bool ok = WritePluginVolume(kVanillaMasterKey, master);
                logs::info("Adopted Vanilla master: {:.4f} -> {:.4f} ({})", g_savedMaster, master, ok ? "stored" : "NOT STORED");
                g_savedMaster = master;
                ++adopted;
            }
        }
        if (adopted > 0) {
            logs::info("{} value(s) changed outside this plugin taken into the volume store", adopted);
        }
    }

    // Any menu closing stores sliders that were moved but never persisted;
    // the Journal Menu (Settings > Audio) closing also adopts what the
    // Vanilla menu changed.
    class MenuCloseSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (a_event && !a_event->opening && g_ready.load()) {
                const char* name = a_event->menuName.c_str();
                const bool  journal = name && std::string_view{ name } == "Journal Menu"sv;
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([journal]() {
                        QAS::Audio::FlushDirty();
                        if (!journal) {
                            return;
                        }
                        // Queued behind the persist tasks FlushDirty() added.
                        if (auto* later = SKSE::GetTaskInterface()) {
                            later->AddTask([]() { AdoptExternalChanges(); });
                        }
                    });
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void RegisterMenuCloseSink()
    {
        static MenuCloseSink sink;
        static bool          registered = false;
        if (registered) {
            return;
        }
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
            registered = true;
        } else {
            logs::warn("UI singleton unavailable; changes made in the Vanilla audio menu are not taken into the store");
        }
    }
}

namespace QAS::Audio
{
    void Initialize()
    {
        g_entries.clear();
        g_groups.clear();
        g_prefsPath = ResolvePrefsPath();
        logs::info("Preference INI: '{}'", g_prefsPath);

        // The Vanilla master (fAudioMasterVolume / BSAudioManager::masterVolume)
        // has no slider here (the "Alle Sounds" multiplier covers every root
        // category instead), but its value is kept in the volume store like
        // the sliders', so it survives a SkyrimPrefs.ini that is not saved.
        bool  repairMaster = false;
        float currentMaster = -1.0F;
        if (const auto* setting = FindPref(kMasterSetting)) {
            currentMaster = Clamp01(setting->data.f);
            logs::info("Vanilla master: {} = {:.4f}", kMasterSetting, setting->data.f);
            repairMaster = setting->data.f <= 0.0F;
        }

        // Existing [AudioMenu] slot assignment: uID<n> -> FormID.
        std::vector<std::pair<int, std::uint32_t>> slots;
        for (int slot = 0; slot < kMaxSlots; ++slot) {
            if (auto id = ReadSlotFormID(slot)) {
                slots.emplace_back(slot, *id);
            }
        }
        // Lowest free slot first, so the engine's eight registered slots
        // (uID0..uID7) are used before falling back to INI-only lines.
        auto nextFreeSlot = [&slots]() {
            int slot = 0;
            while (std::any_of(slots.begin(), slots.end(), [slot](const auto& a_pair) { return a_pair.first == slot; })) {
                ++slot;
            }
            return slot;
        };
        int registeredSlots = 0;
        while (registeredSlots < kMaxSlots && FindPref(std::format("uID{}:{}", registeredSlots, kSection))) {
            ++registeredSlots;
        }
        logs::info("[AudioMenu] has {} assigned slot(s); the engine registers {} slot setting(s)",
            slots.size(), registeredSlots);
        g_engineSlots = registeredSlots > 0 ? registeredSlots : kEngineSlotsFallback;
        if (registeredSlots == 0) {
            logs::warn("No uID<n> setting registered; assuming {} engine slots", g_engineSlots);
        }
        g_pluginIniPath = ResolvePluginIniPath();
        EnsurePluginIni();
        g_fallbackIniPath.clear();
        g_fallbackActive = false;
        if (auto logDir = SKSE::log::log_directory()) {
            std::error_code ec;
            g_fallbackIniPath = (*logDir / "QuickAudioSliders.ini").string();
            g_fallbackActive = std::filesystem::exists(std::filesystem::path(g_fallbackIniPath), ec);
        }
        logs::info("Volume store: '{}' with {} [Volumes] line(s); fallback '{}' {}", g_pluginIniPath,
            CountStoreValues(g_pluginIniPath), g_fallbackIniPath,
            g_fallbackActive ? std::format("with {} line(s)", CountStoreValues(g_fallbackIniPath)) : "not in use"s);
        const bool modCategoriesEnabled = ReadIniInt(kModCategoriesSection, QAS::Ini::kModCategoriesEnabledKey,
                                              QAS::Ini::kModCategoriesEnabledDefault ? 1 : 0) != 0;
        const int  modCategoriesMinSounds = ReadIniInt(kModCategoriesSection, "MinSounds", 3);
        const bool weatherCategoriesEnabled = ReadIniInt(kModCategoriesSection, "Weather", 1) != 0;
        logs::info("[ModCategories] Enabled={} MinSounds={} Weather={}", modCategoriesEnabled ? 1 : 0,
            modCategoriesMinSounds, weatherCategoriesEnabled ? 1 : 0);

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logs::error("TESDataHandler unavailable; no sound categories enumerated");
            g_ready.store(true);
            return;
        }

        g_importedTranslations.clear();
        for (auto* category : dataHandler->GetFormArray<RE::BGSSoundCategory>()) {
            if (!category || !category->IsMenuCategory()) {
                continue;
            }
            auto entry = std::make_unique<Entry>();
            entry->category = category;
            entry->formID = category->GetFormID();
            InitEntryLabel(*entry);

            for (const auto& [slot, id] : slots) {
                if (id == entry->formID) {
                    entry->slot = slot;
                    break;
                }
            }
            if (entry->slot < 0) {
                entry->slot = nextFreeSlot();
                slots.emplace_back(entry->slot, entry->formID);
            }

            entry->uiVolume = ReadVolume(*entry);
            // volumeMult is the menu slider value; GetCategoryVolume() is the
            // effective volume (volumeMult * static multiplier). CommonLib's
            // GetStaticVolumeMultiplier() truncates to int, so compute it here.
            logs::info("Category {:08X} {} '{}' slot {} volumeMult {:.4f} GetCategoryVolume {:.4f} staticMult {:.3f}",
                entry->formID, entry->editorID, entry->label, entry->slot,
                category->volumeMult, category->GetCategoryVolume(),
                static_cast<float>(category->staticMult) / 65535.0F);
            g_entries.push_back(std::move(entry));
        }

        // One runtime category per mod (and original parent category).
        g_modPlans.clear();
        if (modCategoriesEnabled) {
            AddUnlistedModCategories(dataHandler);
            PlanModCategories(dataHandler, modCategoriesMinSounds);
            if (weatherCategoriesEnabled) {
                PlanWeatherCategories(dataHandler);
            }
            CreateModCategories();
        }

        // Every slider is restored from the volume store. An INI written by
        // a version before the store owned the engine-slot categories can be
        // older than SkyrimPrefs.ini for those (the Vanilla menu changed
        // them without the plugin noticing): on the first start of this
        // version a stored value is only trusted where it matches the
        // engine's fVal<n>, otherwise the current value is taken over.
        struct Restore
        {
            std::size_t index;
            float       volume;
            float       found;
        };
        const bool           ownsAll = ReadPluginVolume(kOwnsAllKey).has_value();
        const auto           migrationGlobal = ReadPluginVolume(kGlobalKey);
        std::vector<Restore> restores;
        std::size_t          withoutValue = 0;
        for (std::size_t index = 0; index < g_entries.size(); ++index) {
            auto&       entry = *g_entries[index];
            const float current = ReadVolume(entry);
            entry.savedVolume = current;
            const auto key = PluginIniKey(entry);
            if (key.empty()) {
                logs::warn("Category {:08X} '{}' has no defining file; cannot persist it", entry.formID, entry.editorID);
                continue;
            }
            const auto stored = ReadPluginVolume(key);
            if (!stored) {
                ++withoutValue;
                continue;
            }
            if (!ownsAll && HasEngineSlot(entry)) {
                const auto* slotValue = FindPref(std::format("fVal{}:{}", entry.slot, kSection));
                // A root that is also a menu slider holds baseline x global
                // while the multiplier is active, and the engine saves that
                // scaled value on exit: fVal<n> then equals stored x global,
                // and the stored baseline is still the right one.
                const bool  scaledRoot = !entry.category->parentCategory && migrationGlobal &&
                                         *migrationGlobal < kGlobalIdentity;
                const float scale = scaledRoot ? *migrationGlobal : 1.0F;
                const bool  matches =
                    slotValue && (std::abs(slotValue->data.f - *stored) <= kVolumeEpsilon ||
                                     (scaledRoot && (scale <= kVolumeEpsilon ||
                                                        std::abs(slotValue->data.f - *stored * scale) <= 2.0F * kVolumeEpsilon)));
                if (!matches) {
                    // Changed in the Vanilla menu since. For a scaled root the
                    // current value includes the multiplier: take it out, the
                    // multiplier is applied again below.
                    const float adopted = scaledRoot ? Clamp01(current / scale) : current;
                    const bool  ok = WritePluginVolume(key, adopted);
                    logs::info("Category {:08X} '{}' (slot {}) key '{}': stored {:.4f} predates SkyrimPrefs.ini, "
                               "taking over the current {:.4f}{} ({})",
                        entry.formID, entry.label, entry.slot, key, *stored, adopted,
                        scaledRoot ? std::format(" (found {:.4f} with the global multiplier {:.4f})", current, scale) : ""s,
                        ok ? "stored" : "NOT STORED");
                    if (scaledRoot) {
                        entry.uiVolume = adopted;
                        entry.savedVolume = adopted;
                        restores.push_back(Restore{ index, adopted, current });
                    }
                    continue;
                }
            }
            entry.uiVolume = *stored;
            entry.savedVolume = *stored;
            restores.push_back(Restore{ index, *stored, current });
        }
        logs::info("{} slider(s) have a stored volume, {} have none yet (they keep the value found at startup)",
            restores.size(), withoutValue);
        if (!ownsAll) {
            WritePluginVolume(kOwnsAllKey, 1.0F);
        }

        // Vanilla master: a stored value wins over what the engine loaded.
        const auto storedMaster = ReadPluginVolume(kVanillaMasterKey);
        g_savedMaster = storedMaster.value_or(currentMaster);
        const bool restoreMaster =
            storedMaster && currentMaster >= 0.0F && std::abs(*storedMaster - currentMaster) > kVolumeEpsilon;
        if (storedMaster) {
            repairMaster = false;  // the store decides, including a deliberate 0
        }

        BuildGroups();
        CollectRoots(dataHandler);
        logs::info("{} category slider(s) in {} group(s); global slider drives {} root(s)",
            g_entries.size(), g_groups.size(), g_roots.size());

        // Apply the restored volumes on the game thread, then the saved
        // global multiplier (after the categories, so root baselines are the
        // restored values), then report ready.
        const auto savedGlobal = ReadPluginVolume(kGlobalKey);
        // One-time repair of a Vanilla master left at 0 by an older build:
        // only when the plugin INI carries no marker yet, so a master the
        // user deliberately sets to 0 in the Vanilla menu later is respected.
        if (repairMaster && ReadPluginVolume(kMasterRepairedKey).has_value()) {
            logs::info("Vanilla master is 0 but was repaired once before; leaving it to the Vanilla menu");
            repairMaster = false;
        }
        auto applyRestores = [restores = std::move(restores), savedGlobal, repairMaster, restoreMaster, storedMaster]() {
            if (repairMaster) {
                RestoreVanillaMaster("startup repair: fAudioMasterVolume was 0");
                WritePluginVolume(kMasterRepairedKey, 1.0F);
            } else if (restoreMaster) {
                ApplyVanillaMaster(*storedMaster);
                logs::info("Vanilla master restored to {:.4f} from the volume store", *storedMaster);
            }
            RepointModDescriptors();
            std::size_t overridden = 0;
            for (const auto& restore : restores) {
                auto& entry = *g_entries[restore.index];
                // What the category holds now, after every kDataLoaded
                // listener ran (Engine Fixes applies its own file there).
                const float before = entry.category->volumeMult;
                const bool  differs = std::abs(before - restore.volume) > kVolumeEpsilon ||
                                     std::abs(restore.found - restore.volume) > kVolumeEpsilon;
                ApplyNow(entry, restore.volume);
                if (!differs) {
                    continue;
                }
                ++overridden;
                logs::info("Restored {:08X} {} '{}' (slot {}) key '{}' volume {:.4f} (was {:.4f})", entry.formID,
                    entry.editorID, entry.label, entry.slot, PluginIniKey(entry), restore.volume, before);
                if (HasEngineSlot(entry)) {
                    // Keep [AudioMenu] in step, so the Vanilla menu shows the
                    // restored value and the engine saves it from now on.
                    WriteUIntPref(std::format("uID{}", entry.slot), entry.formID);
                    WriteFloatPref(std::format("fVal{}", entry.slot), restore.volume);
                }
            }
            logs::info("{} of {} stored volume(s) differed from what the game had loaded", overridden, restores.size());
            if (savedGlobal) {
                g_global.store(*savedGlobal);
                ApplyGlobalNow(*savedGlobal);
                logs::info("Restored global multiplier {:.4f} from '{}'", *savedGlobal, g_pluginIniPath);
            }
            // Baseline for "changed outside this plugin": what every slider
            // holds now that all kDataLoaded listeners and the store ran.
            for (auto& entry : g_entries) {
                entry->savedVolume = ReadVolume(*entry);
            }
            logs::info("{} volume(s) applied from the volume store; sliders ready", restores.size());
            g_ready.store(true);
            RegisterMenuCloseSink();
        };
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask(std::move(applyRestores));
        } else {
            applyRestores();  // kDataLoaded is delivered on the game thread
        }
    }

    float GetGlobal()
    {
        return g_global.load();
    }

    std::size_t RootCount()
    {
        return g_roots.size();
    }

    void SetGlobal(float a_multiplier)
    {
        const auto multiplier = Clamp01(a_multiplier);
        g_global.store(multiplier);
        g_globalDirty.store(true);
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([multiplier]() { ApplyGlobalNow(multiplier); });
        }
    }

    bool GlobalDirty()
    {
        return g_globalDirty.load();
    }

    void FlushDirty()
    {
        for (std::size_t index = 0; index < g_entries.size(); ++index) {
            if (g_entries[index]->dirty.load() && !g_entries[index]->pending.load()) {
                logs::info("Storing '{}' now: it was moved but never released", g_entries[index]->label);
                Persist(index);
            }
        }
        if (g_globalDirty.load()) {
            PersistGlobal();
        }
    }

    void PersistGlobal()
    {
        const auto multiplier = Clamp01(g_global.load());
        g_globalDirty.store(false);
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([multiplier]() {
                const auto ok = WritePluginVolume(kGlobalKey, multiplier);
                logs::info("Persisted [{}] {} = {:.4f} ({})", kVolumesSection, kGlobalKey, multiplier,
                    ok ? "ok" : "FAILED");
                // A root that is also a menu slider carries baseline x global
                // in volumeMult, and other plugins (Engine Fixes) save that
                // scaled value. Store the baseline, so the next start puts it
                // back before the multiplier is applied again.
                for (auto& entryPtr : g_entries) {
                    auto& entry = *entryPtr;
                    if (entry.synthetic || !FindRoot(entry.category)) {
                        continue;
                    }
                    const auto  key = PluginIniKey(entry);
                    const float baseline = ReadVolume(entry);
                    if (!key.empty() && WritePluginVolume(key, baseline)) {
                        entry.savedVolume = baseline;
                    }
                }
            });
        }
    }

    const std::vector<Group>& Groups()
    {
        return g_groups;
    }

    void RefreshTranslatedLabels()
    {
        std::size_t changed = 0;
        for (auto& entry : g_entries) {
            if (!entry->labelPendingTranslation || entry->labelOverridden) {
                continue;
            }
            auto newLabel = Translation::Translate(entry->rawFullName);
            if (newLabel == entry->rawFullName.substr(1)) {
                continue;  // still not in the table
            }
            entry->labelPendingTranslation = false;
            // Re-apply the cosmetics the label went through at build time.
            if (!entry->labelModFile.empty()) {
                newLabel = StripModPrefix(newLabel, entry->labelModFile);
                if (entry->labelPooled) {
                    newLabel = PooledLabel(entry->labelModFile, newLabel);
                }
            }
            newLabel = TitleCaseIfAllCaps(newLabel);
            if (newLabel != entry->label) {
                logs::info("Label for {:08X} re-translated: '{}' -> '{}'", entry->formID, entry->label, newLabel);
                entry->label = newLabel;
                entry->widgetID = std::format("{}##{:08X}", entry->label, entry->formID);
                ++changed;
            }
        }
        if (changed > 0) {
            logs::info("{} label(s) re-translated on first render", changed);
        }
    }

    bool IsReady()
    {
        return g_ready.load();
    }

    std::size_t Count()
    {
        return g_entries.size();
    }

    Entry& Get(std::size_t a_index)
    {
        return *g_entries[a_index];
    }

    float ReadVolume(const Entry& a_entry)
    {
        // Root menu categories show their own (baseline) value while the
        // global multiplier scales them (relaxed reads; written on the game
        // thread).
        if (g_globalActive.load(std::memory_order_relaxed)) {
            for (const auto& root : g_roots) {
                if (root.category == a_entry.category) {
                    return Clamp01(root.baseline.load(std::memory_order_relaxed));
                }
            }
        }
        return Clamp01(a_entry.category->volumeMult);
    }

    void Preview(std::size_t a_index, float a_volume)
    {
        if (a_index >= g_entries.size()) {
            return;
        }
        auto&      entry = *g_entries[a_index];
        const auto volume = Clamp01(a_volume);
        entry.uiVolume = volume;
        entry.dirty.store(true);
        entry.pending.store(true);
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([a_index, volume]() {
                auto& target = *g_entries[a_index];
                ApplyNow(target, volume);
                target.pending.store(false);
            });
        }
    }

    float DefaultVolume(const Entry& a_entry)
    {
        // CommonLib's GetDefaultMenuValue() divides two integers; do it in float.
        const auto raw = a_entry.category->defaultMenuValue;
        return raw == 0 ? 1.0F : Clamp01(static_cast<float>(raw) / 65535.0F);
    }

    void ResetEntry(std::size_t a_index)
    {
        if (a_index >= g_entries.size()) {
            return;
        }
        auto&      entry = *g_entries[a_index];
        const auto volume = DefaultVolume(entry);
        logs::info("Reset {} '{}' to its default {:.4f}", entry.editorID, entry.label, volume);
        Preview(a_index, volume);
        Persist(a_index);
    }

    void ResetAll()
    {
        logs::info("Reset all {} slider(s) to their defaults, the global multiplier and the Vanilla master to 100%",
            g_entries.size());
        // Global first: releasing the roots to their baseline must run before
        // the category resets, otherwise a root that is also a menu category
        // (_SPPF_SFX, WordWalls) would be overwritten with its old baseline.
        SetGlobal(1.0F);
        PersistGlobal();
        for (std::size_t index = 0; index < g_entries.size(); ++index) {
            ResetEntry(index);
        }
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([]() { RestoreVanillaMaster("reset all"); });
        }
    }

    void Persist(std::size_t a_index)
    {
        if (a_index >= g_entries.size()) {
            return;
        }
        const auto volume = Clamp01(g_entries[a_index]->uiVolume);
        g_entries[a_index]->dirty.store(false);
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([a_index, volume]() {
                auto& entry = *g_entries[a_index];
                ApplyNow(entry, volume);
                // The volume store holds every slider and is what the next
                // start applies.
                if (const auto key = PluginIniKey(entry); !key.empty()) {
                    const auto ok = WritePluginVolume(key, volume);
                    logs::info("Persisted [{}] {} = {:.4f} ({})", kVolumesSection, key, volume, ok ? "ok" : "FAILED");
                }
                entry.savedVolume = volume;
                if (!HasEngineSlot(entry)) {
                    return;  // no engine slot: nothing to mirror
                }
                // Always write the uID line too: the engine re-assigns free
                // slots in memory at startup (InitItemImpl) without writing
                // them back, so the on-disk uID<n> may still hold a stale
                // FormID from an older load order. A mismatched uID makes the
                // engine discard fVal<n> on the next start.
                WriteUIntPref(std::format("uID{}", entry.slot), entry.formID);
                WriteFloatPref(std::format("fVal{}", entry.slot), volume);
            });
        }
    }
}
