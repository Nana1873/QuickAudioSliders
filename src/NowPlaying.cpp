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
    // A looping sound is requested once and then plays on, so it would drop
    // out while it is still heard. A loop therefore stays listed until the
    // next loading screen; a new place starts its own loops, and what played
    // in the old one is of no interest there. Two limits: when a loop stops is
    // not known here, so a listed one may be silent by now; and a loop that
    // plays on across a loading screen without being requested again is no
    // longer listed.

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
        bool                          loop{ false };
        std::uint32_t                 place{ 0 };  // g_place when last requested
    };

    std::mutex                 g_mutex;  // guards g_hits; the hook runs on whichever thread asks for a sound
    std::vector<Hit>           g_hits;
    std::atomic<std::uint32_t> g_place{ 0 };  // counts loading screens

    class LoadingScreenSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (a_event && a_event->opening && a_event->menuName == RE::LoadingMenu::MENU_NAME) {
                g_place.fetch_add(1, std::memory_order_relaxed);
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void Record(const RE::BGSStandardSoundDef* a_descriptor)
    {
        const auto            now = Clock::now();
        const bool            loop = a_descriptor->lengthCharacteristics.looping.any(
            RE::BGSStandardSoundDef::LengthCharacteristics::Looping::kLoop);
        const std::lock_guard lock(g_mutex);
        for (auto& hit : g_hits) {
            if (hit.descriptor == a_descriptor) {
                hit.category = a_descriptor->category;
                hit.loop = loop;
                hit.place = g_place.load(std::memory_order_relaxed);
                hit.last = now;
                ++hit.count;
                return;
            }
        }
        if (g_hits.size() >= kMaxHits) {
            // Full: the oldest one-shot goes first, a loop only when nothing else is left.
            const auto oldest = std::min_element(g_hits.begin(), g_hits.end(), [](const Hit& a_lhs, const Hit& a_rhs) {
                return a_lhs.loop != a_rhs.loop ? !a_lhs.loop : a_lhs.last < a_rhs.last;
            });
            g_hits.erase(oldest);
        }
        g_hits.push_back(Hit{ a_descriptor, a_descriptor->category, now, 1, loop, g_place.load(std::memory_order_relaxed) });
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

    // The engine does not keep the editor ID of a sound descriptor: its
    // SetFormEditorID (1.6.1170: 140324160) only files a hash of the name for
    // GetSoundHandleByName. So the names are noted here while the records
    // load, until Install() has copied them into the index. An ID changed in
    // a running game is not followed.
    std::mutex                                                         g_namesMutex;
    std::unordered_map<const RE::BGSSoundDescriptorForm*, std::string> g_names;
    std::atomic<bool>                                                  g_namesOpen{ true };

    struct SetEditorIDHook
    {
        static bool thunk(RE::BGSSoundDescriptorForm* a_this, const char* a_editorID)
        {
            if (a_editorID && *a_editorID && g_namesOpen.load(std::memory_order_relaxed)) {
                const std::lock_guard lock(g_namesMutex);
                g_names.insert_or_assign(a_this, a_editorID);
            }
            return func(a_this, a_editorID);
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

    void InstallNameCapture()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BGSSoundDescriptorForm[0] };
        SetEditorIDHook::func = vtable.write_vfunc(0x33, SetEditorIDHook::thunk);
        logs::info("Now Playing: noting the editor IDs of sound descriptors while the game data loads");
    }

    void Install()
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logs::warn("Now Playing: TESDataHandler unavailable; page stays empty");
            return;
        }
        g_namesOpen.store(false);
        const std::lock_guard namesLock(g_namesMutex);
        g_index.clear();
        std::size_t named = 0;
        for (auto* form : dataHandler->GetFormArray<RE::BGSSoundDescriptorForm>()) {
            if (!form || !form->soundDescriptor) {
                continue;
            }
            Info        info;
            const auto* file = form->GetFile(0);
            info.plugin = file ? std::string(file->GetFilename()) : std::string();
            // The name noted while loading, else what an editor-ID restoring
            // plugin may answer, else plugin and FormID.
            const char* editorID = form->GetFormEditorID();
            if (const auto it = g_names.find(form); it != g_names.end()) {
                info.name = it->second;
                ++named;
            } else if (editorID && *editorID) {
                info.name = editorID;
                ++named;
            } else {
                info.name = std::format("{}|{:06X}", info.plugin, form->GetLocalFormID());
            }
            g_index.emplace(form->soundDescriptor, std::move(info));
        }
        g_names.clear();  // copied into the index
        logs::info("Now Playing: {} of {} sound descriptor(s) have a name", named, g_index.size());

        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BGSStandardSoundDef[0] };
        DoResolveHook::func = vtable.write_vfunc(1, DoResolveHook::thunk);
        static LoadingScreenSink sink;
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
        }
        logs::info("Now Playing: {} sound descriptor(s) indexed; BGSStandardSoundDef::DoResolve hooked "
                   "(keeps the last {} distinct sounds for {:.0f} s, loops until the next loading screen)",
            g_index.size(), kMaxHits, kKeepSeconds);
    }

    std::vector<Row> Snapshot()
    {
        const auto       now = Clock::now();
        const auto       place = g_place.load(std::memory_order_relaxed);
        std::vector<Hit> hits;
        {
            const std::lock_guard lock(g_mutex);
            std::erase_if(g_hits, [&](const Hit& a_hit) {
                // A loading screen empties the list: what was requested in
                // the old place is of no interest in the new one. Within a
                // place a loop stays, a single sound for kKeepSeconds.
                if (a_hit.place != place) {
                    return true;
                }
                const float age = std::chrono::duration<float>(now - a_hit.last).count();
                return !a_hit.loop && age > kKeepSeconds;
            });
            hits = g_hits;
        }
        // Loops first (most likely still heard), then one-shots; newest first.
        std::sort(hits.begin(), hits.end(), [](const Hit& a_lhs, const Hit& a_rhs) {
            return a_lhs.loop != a_rhs.loop ? a_lhs.loop : a_lhs.last > a_rhs.last;
        });

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
            row.loop = hit.loop;
            rows.push_back(std::move(row));
        }
        return rows;
    }
}
