#include "AudioSliders.h"
#include "MenuIntegration.h"
#include "Translation.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    void InitializeLog()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Failed to find standard logging directory"sv);
        }
        const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
        *path /= std::format("{}.log", plugin->GetName());
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S:%e] [%l] %v"s);
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_message)
    {
        switch (a_message->type) {
        case SKSE::MessagingInterface::kPostLoad:
            QAS::MenuIntegration::Initialize();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            // Data/Interface/Translations/QuickAudioSliders_<LANGUAGE>.txt
            // (English defaults are compiled in; German ships alongside).
            QAS::Translation::Import("QuickAudioSliders");
            QAS::Translation::LoadStrings();
            QAS::Audio::Initialize();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    InitializeLog();
    SKSE::Init(a_skse, false);

    const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
    logs::info("{} v{} loaded (runtime {})",
        plugin->GetName(),
        plugin->GetVersion().string(),
        REL::Module::get().version().string());

    if (a_skse->IsEditor()) {
        logs::info("Creation Kit detected; plugin disabled");
        return false;
    }

    SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);
    return true;
}
