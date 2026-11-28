#include "Translation.h"

#include "StringConv.h"

#include <RE/B/BSResourceNiBinaryStream.h>
#include <RE/I/INISettingCollection.h>
#include <SKSE/Translation.h>

namespace
{
    QAS::Translation::Strings g_strings;

    // Our own table for the files Import() parses. Never insert into the
    // engine's Scaleform table: InitTESThread may be rebuilding it, which
    // intermittently crashed startup (see design-notes, 2026-10-27).
    // Written only at kDataLoaded, read-only afterwards. Keys are stored
    // lowercased: the engine table this replaces matched case-insensitively
    // (interned BSFixedStringW), and mod files rely on that.
    std::unordered_map<std::string, std::string> g_table;

    // Set on the first menu render (EnableEngineLookup); before that the
    // game's translator is not consulted at all.
    std::atomic<bool> g_engineLookup{ false };

    std::string LowerKey(std::string_view a_key)
    {
        auto wide = QAS::Text::ToWide(a_key);
        if (!wide.empty()) {
            ::CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
        }
        return QAS::Text::ToUtf8(wide);
    }

    std::string GameLanguage()
    {
        auto*       collection = RE::INISettingCollection::GetSingleton();
        const auto* setting = collection ? collection->GetSetting("sLanguage:General") : nullptr;
        if (setting && setting->GetType() == RE::Setting::Type::kString && setting->data.s && *setting->data.s) {
            return setting->data.s;
        }
        return "ENGLISH";
    }

    // BSResourceNiBinaryStream so BSA-packed files resolve like loose ones.
    std::uint32_t ReadLineW(RE::BSResourceNiBinaryStream& a_stream, wchar_t* a_dst, std::uint32_t a_dstLen, bool& a_eof)
    {
        wchar_t* iter = a_dst;
        if (a_dstLen == 0) {
            a_eof = true;
            return 0;
        }
        for (std::uint32_t i = 0; i < a_dstLen - 1; ++i) {
            wchar_t       data = 0;
            std::uint64_t read = 0;
            a_stream.stream->DoRead(&data, sizeof(data), read);
            if (read != sizeof(data)) {
                a_eof = true;
                break;
            }
            if (data == L'\n') {
                break;
            }
            *iter++ = data;
        }
        *iter = 0;
        return static_cast<std::uint32_t>(iter - a_dst);
    }

    bool ParseFile(const std::string& a_path)
    {
        RE::BSResourceNiBinaryStream fileStream{ a_path };
        if (!fileStream.good()) {
            return false;
        }
        std::uint16_t bom = 0;
        std::uint64_t read = 0;
        fileStream.stream->DoRead(&bom, sizeof(bom), read);
        if (read != sizeof(bom) || bom != 0xFEFF) {
            // Empty or not UTF-16 LE (a common packaging mistake); treat it
            // as absent so Import() can fall back to the ENGLISH file.
            logs::warn("{}: missing UTF-16 LE BOM; file skipped", a_path);
            return false;
        }
        std::size_t entries = 0;
        bool        eof = false;
        while (!eof) {
            wchar_t             buf[512];
            const std::uint32_t rawLen = ReadLineW(fileStream, buf, static_cast<std::uint32_t>(std::size(buf)), eof);
            if (rawLen == 0) {
                continue;  // blank line; EOF ends the loop via eof
            }
            std::uint32_t len = rawLen;
            if (buf[len - 1] == L'\r') {
                buf[--len] = 0;
            }
            if (len < 4 || buf[0] != L'$') {
                continue;
            }
            std::uint32_t delim = 0;
            for (std::uint32_t i = 0; i < len; ++i) {
                if (buf[i] == L'\t') {
                    delim = i;
                }
            }
            if (delim < 2) {
                continue;
            }
            buf[delim] = 0;
            ::CharLowerBuffW(buf, delim);
            auto key = QAS::Text::ToUtf8(buf);
            auto value = QAS::Text::ToUtf8(&buf[delim + 1]);
            if (!key.empty() && !value.empty() && g_table.emplace(std::move(key), std::move(value)).second) {
                ++entries;
            }
        }
        logs::info("{}: {} translation(s) imported", a_path, entries);
        return true;
    }

    // The printf argument signature of a format string: every conversion
    // specifier's length modifiers and final character plus any '*'
    // width/precision ("%.0f%%" -> "f", "%*d" -> "*d", "%lld" -> "lld");
    // std::nullopt for a malformed one (lone trailing '%'). "%%" consumes
    // nothing and is skipped.
    std::optional<std::string> FormatSpecs(std::string_view a_text)
    {
        std::string specs;
        for (std::size_t i = 0; i < a_text.size(); ++i) {
            if (a_text[i] != '%') {
                continue;
            }
            if (++i >= a_text.size()) {
                return std::nullopt;
            }
            if (a_text[i] == '%') {
                continue;
            }
            while (i < a_text.size()) {
                const char ch = a_text[i];
                if (ch == '*' || std::string_view{ "hljztL" }.find(ch) != std::string_view::npos) {
                    specs.push_back(ch);  // changes what the argument is / how it is read
                } else if (std::string_view{ "-+ #0.123456789" }.find(ch) == std::string_view::npos) {
                    break;
                }
                ++i;
            }
            if (i >= a_text.size()) {
                return std::nullopt;
            }
            specs.push_back(a_text[i]);
        }
        return specs;
    }

    // For strings that are handed to printf-style ImGui calls as the format:
    // a translation whose placeholders do not consume exactly the arguments
    // the English default does would be undefined behavior, so it is rejected
    // and the default kept.
    std::string TranslateFormat(const char* a_key, const std::string& a_default)
    {
        auto result = QAS::Translation::Translate(a_key, a_default);
        if (FormatSpecs(result) != FormatSpecs(a_default)) {
            logs::warn("Translation for {} has mismatched format placeholders ('{}'); using the English default",
                a_key, result);
            return a_default;
        }
        return result;
    }

    std::optional<std::string> Lookup(std::string_view a_key)
    {
        if (const auto it = g_table.find(LowerKey(a_key)); it != g_table.end()) {
            return it->second;
        }

        // Then the translator's virtual Translate(),
        // exactly what Scaleform calls for "$Key" strings in menus. It
        // covers the engine's own table (interface/translate_<lang>.txt).
        //
        // No raw BSScaleformTranslator::translationMap scan after this (1.1.0
        // had one). Its find() crashed at kDataLoaded on a key slot holding
        // UTF-16 text instead of a string-pool entry (Nexus CTD report
        // 2026-11-27: SexLab $SSL_SFXVolume, ScaleformTranslationPP 1.8 and
        // ~300 SKSE plugins; not reproduced in the lab with STPP 1.10). The
        // kTranslator state need not be a stock BSScaleformTranslator, so
        // its members are never read directly; the virtual call goes
        // through whichever translator is installed (as in Notification
        // Tweaks). And not before the first menu render: at kDataLoaded the
        // engine may still be building that table (see EnableEngineLookup).
        if (!g_engineLookup.load()) {
            return std::nullopt;
        }
        if (std::string result; SKSE::Translation::Translate(std::string(a_key), result) && !result.empty() &&
                                result != a_key) {
            return result;
        }
        return std::nullopt;
    }
}

namespace QAS::Translation
{
    void Import(std::string_view a_name)
    {
        const auto language = GameLanguage();
        const auto path = std::format("Interface\\Translations\\{}_{}.txt", a_name, language);
        if (!ParseFile(path) && language != "ENGLISH") {
            ParseFile(std::format("Interface\\Translations\\{}_ENGLISH.txt", a_name));
        }
    }

    void EnableEngineLookup()
    {
        g_engineLookup.store(true);
    }

    std::string Translate(std::string_view a_key, std::string_view a_fallback)
    {
        if (a_key.empty() || a_key.front() != '$') {
            return std::string(a_key);
        }
        if (auto translated = Lookup(a_key)) {
            return *translated;
        }
        return std::string(a_fallback.empty() ? a_key.substr(1) : a_fallback);
    }

    void LoadStrings()
    {
        Strings defaults;
        g_strings.globalHeading = Translate("$QAS_GlobalHeading", defaults.globalHeading);
        g_strings.global = Translate("$QAS_Global", defaults.global);
        g_strings.noRoots = Translate("$QAS_NoRoots", defaults.noRoots);
        g_strings.resetAll = Translate("$QAS_ResetAll", defaults.resetAll);
        g_strings.resetAllConfirm = Translate("$QAS_ResetAllConfirm", defaults.resetAllConfirm);
        g_strings.resetAllTip = Translate("$QAS_ResetAllTip", defaults.resetAllTip);
        g_strings.reset = Translate("$QAS_Reset", defaults.reset);
        g_strings.resetTip = TranslateFormat("$QAS_ResetTip", defaults.resetTip);
        g_strings.vanillaGame = Translate("$QAS_VanillaGame", defaults.vanillaGame);
        g_strings.otherMods = Translate("$QAS_OtherMods", defaults.otherMods);
        g_strings.footer = Translate("$QAS_Footer", defaults.footer);
        g_strings.modsPageHint = Translate("$QAS_ModsPageHint", defaults.modsPageHint);
        g_strings.modsPageEmpty = Translate("$QAS_ModsPageEmpty", defaults.modsPageEmpty);
        g_strings.waiting = Translate("$QAS_Waiting", defaults.waiting);
        g_strings.noCategories = Translate("$QAS_NoCategories", defaults.noCategories);
        g_strings.nowPlayingHint = Translate("$QAS_NowPlayingHint", defaults.nowPlayingHint);
        g_strings.nowPlayingEmpty = Translate("$QAS_NowPlayingEmpty", defaults.nowPlayingEmpty);
        g_strings.nowPlayingLimits = Translate("$QAS_NowPlayingLimits", defaults.nowPlayingLimits);
        g_strings.colSound = Translate("$QAS_ColSound", defaults.colSound);
        g_strings.colMod = Translate("$QAS_ColMod", defaults.colMod);
        g_strings.colSlider = Translate("$QAS_ColSlider", defaults.colSlider);
        g_strings.colAge = Translate("$QAS_ColAge", defaults.colAge);
        g_strings.noSlider = Translate("$QAS_NoSlider", defaults.noSlider);
        g_strings.extraPageTag = Translate("$QAS_ExtraPageTag", defaults.extraPageTag);
        g_strings.sectionLoops = Translate("$QAS_SectionLoops", defaults.sectionLoops);
        g_strings.sectionSingle = Translate("$QAS_SectionSingle", defaults.sectionSingle);
        logs::info("UI strings loaded ('{}' / '{}')", g_strings.resetAll, g_strings.otherMods);
    }

    const Strings& S()
    {
        return g_strings;
    }
}
