#include "NowPlaying.h"

#include "AudioSliders.h"
#include "PluginIni.h"

#include <chrono>
#include <mutex>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr std::size_t  kMaxHits = 64;         // distinct descriptors kept
    constexpr float        kKeepSeconds = 120.0F;  // a sound leaves the list this long after its last request

    struct Info
    {
        std::string name;
        std::string plugin;
    };

    // Built once in Install(), read-only afterwards (the hook only reads it
    // through Snapshot(), on the render thread).
    std::unordered_map<const RE::BGSSoundDescriptor*, Info> g_index;

    struct Hit
    {
        const RE::BGSSoundDescriptor* descriptor{ nullptr };
        const RE::BGSSoundCategory*   category{ nullptr };  // as routed when requested
        Clock::time_point             last;
        std::uint32_t                 count{ 0 };
    };

    std::mutex                g_mutex;  // guards g_hits; the hook runs on whichever thread asks for a sound
    std::vector<Hit>          g_hits;

    void Record(const RE::BGSStandardSoundDef* a_descriptor)
    {
        const auto            now = Clock::now();
        const std::lock_guard lock(g_mutex);
        for (auto& hit : g_hits) {
            if (hit.descriptor == a_descriptor) {
                hit.category = a_descriptor->category;
                hit.last = now;
                ++hit.count;
                return;
            }
        }
        if (g_hits.size() >= kMaxHits) {
            const auto oldest = std::min_element(g_hits.begin(), g_hits.end(),
                [](const Hit& a_lhs, const Hit& a_rhs) { return a_lhs.last < a_rhs.last; });
            g_hits.erase(oldest);
        }
        g_hits.push_back(Hit{ a_descriptor, a_descriptor->category, now, 1 });
    }

    // BSAudioManager::GetSoundHandle asks the descriptor to resolve itself
    // (pick a file, name category and output model) before it creates the
    // sound; a true result is a sound about to be loaded for playback. The
    // call is left untouched, we only note which descriptor it was: one
    // short lock and a scan of at most kMaxHits entries per request. It
    // records all the time, because the menu freezes the game while it is
    // open and the sound in question was heard before. Voice lines and
    // music started by file name do not come through here.
    struct DoResolveHook
    {
        static bool thunk(RE::BGSStandardSoundDef* a_this, RE::BSISoundDescriptor::Resolution& a_resolution)
        {
            const bool resolved = func(a_this, a_resolution);
            if (resolved) {
                Record(a_this);
            }
            return resolved;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };
}

namespace QAS::NowPlaying
{
    bool Enabled()
    {
        std::error_code ec;
        const auto      path = std::filesystem::absolute(QAS::Ini::kPluginIniRelative, ec);
        if (ec || !std::filesystem::exists(path, ec)) {
            return true;  // no INI yet
        }
        return GetPrivateProfileIntA("NowPlaying", "Enabled", 1, path.string().c_str()) != 0;
    }

    void Install()
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logs::warn("Now Playing: TESDataHandler unavailable; page stays empty");
            return;
        }
        g_index.clear();
        for (auto* form : dataHandler->GetFormArray<RE::BGSSoundDescriptorForm>()) {
            if (!form || !form->soundDescriptor) {
                continue;
            }
            Info        info;
            const auto* file = form->GetFile(0);
            info.plugin = file ? std::string(file->GetFilename()) : std::string();
            // Editor IDs of sound descriptors are only kept with an editor-ID
            // restoring plugin (po3 Tweaks, Native EditorID Fix).
            const char* editorID = form->GetFormEditorID();
            info.name = editorID && *editorID ? std::string(editorID) :
                                                std::format("{}|{:06X}", info.plugin, form->GetLocalFormID());
            g_index.emplace(form->soundDescriptor, std::move(info));
        }

        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BGSStandardSoundDef[0] };
        DoResolveHook::func = vtable.write_vfunc(1, DoResolveHook::thunk);
        logs::info("Now Playing: {} sound descriptor(s) indexed; BGSStandardSoundDef::DoResolve hooked "
                   "(keeps the last {} distinct sounds for {:.0f} s)",
            g_index.size(), kMaxHits, kKeepSeconds);
    }

    std::vector<Row> Snapshot()
    {
        const auto       now = Clock::now();
        std::vector<Hit> hits;
        {
            const std::lock_guard lock(g_mutex);
            std::erase_if(g_hits, [&](const Hit& a_hit) {
                return std::chrono::duration<float>(now - a_hit.last).count() > kKeepSeconds;
            });
            hits = g_hits;
        }
        std::sort(hits.begin(), hits.end(), [](const Hit& a_lhs, const Hit& a_rhs) { return a_lhs.last > a_rhs.last; });

        std::vector<Row> rows;
        rows.reserve(hits.size());
        for (const auto& hit : hits) {
            Row row;
            if (const auto it = g_index.find(hit.descriptor); it != g_index.end()) {
                row.sound = it->second.name;
                row.plugin = it->second.plugin;
            } else {
                row.sound = std::format("{:X}", reinterpret_cast<std::uintptr_t>(hit.descriptor));
            }
            QAS::Audio::SliderFor(hit.category, row.slider, row.extraPage);
            row.secondsAgo = std::chrono::duration<float>(now - hit.last).count();
            row.count = hit.count;
            rows.push_back(std::move(row));
        }
        return rows;
    }
}
