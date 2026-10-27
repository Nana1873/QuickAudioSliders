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
    std::uint32_t ReadLineW(RE::BSResourceNiBinaryStream& a_stream, wchar_t* a_dst, std::uint32_t a_dstLen)
    {
        wchar_t* iter = a_dst;
        if (a_dstLen == 0) {
            return 0;
        }
        for (std::uint32_t i = 0; i < a_dstLen - 1; ++i) {
            wchar_t       data = 0;
            std::uint64_t read = 0;
            a_stream.stream->DoRead(&data, sizeof(data), read);
            if (read != sizeof(data) || data == L'\n') {
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
            logs::warn("{}: missing UTF-16 LE BOM; file skipped", a_path);
            return true;  // the file exists; do not fall back to another language
        }
        std::size_t entries = 0;
        while (true) {
            wchar_t             buf[512];
            const std::uint32_t rawLen = ReadLineW(fileStream, buf, static_cast<std::uint32_t>(std::size(buf)));
            if (rawLen == 0) {
                break;
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
        // covers the engine's own table (interface/translate_<lang>.txt),
        // which is not necessarily visible in translationMap.
        if (std::string result; SKSE::Translation::Translate(std::string(a_key), result) && !result.empty() &&
                                result != a_key) {
            return result;
        }

        const auto* manager = RE::BSScaleformManager::GetSingleton();
        if (!manager || !manager->loader) {
            return std::nullopt;
        }
        const auto translator = manager->loader->GetState<RE::BSScaleformTranslator>(RE::GFxState::StateType::kTranslator);
        if (!translator) {
            return std::nullopt;
        }
        const auto& map = translator->translator.translationMap;
        // The engine's own table (interface/translate_<lang>.txt) and the
        // SKSE-imported plugin files may key entries differently ("$Effects"
        // vs "Effects"), so try the key as given and without its '$'.
        for (const auto candidate : { a_key, a_key.substr(1) }) {
            if (candidate.empty()) {
                continue;
            }
            const RE::BSFixedStringW key{ QAS::Text::ToWide(candidate).c_str() };
            const auto               it = map.find(key);
            if (it == map.end()) {
                continue;
            }
            const auto* wide = it->second.c_str();
            if (auto translated = wide ? QAS::Text::ToUtf8(wide) : std::string{}; !translated.empty()) {
                return translated;
            }
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
        logs::info("UI strings loaded ('{}' / '{}')", g_strings.resetAll, g_strings.otherMods);
    }

    const Strings& S()
    {
        return g_strings;
    }
}
