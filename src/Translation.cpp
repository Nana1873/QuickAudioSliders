#include "Translation.h"

#include "StringConv.h"

#include <SKSE/Translation.h>

namespace
{
    QAS::Translation::Strings g_strings;

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
        // The canonical path first: the translator's virtual Translate(),
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
        g_strings.globalHint = Translate("$QAS_GlobalHint", defaults.globalHint);
        g_strings.noRoots = Translate("$QAS_NoRoots", defaults.noRoots);
        g_strings.resetAll = Translate("$QAS_ResetAll", defaults.resetAll);
        g_strings.resetAllConfirm = Translate("$QAS_ResetAllConfirm", defaults.resetAllConfirm);
        g_strings.resetAllTip = TranslateFormat("$QAS_ResetAllTip", defaults.resetAllTip);
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
