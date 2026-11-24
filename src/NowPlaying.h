#pragma once

// "Now Playing" page: the sounds the game requested in the last seconds, each
// with the slider that controls it. See "Now playing" in docs/design-notes.md
// for why this hooks the descriptor and not BSAudioManager::activeSounds.
namespace QAS::NowPlaying
{
    // [NowPlaying] Enabled in the plugin INI (default 1). Read directly from
    // the file, so it also works at kPostLoad.
    [[nodiscard]] bool Enabled();

    // Plugin load, before any record is read: hooks
    // BGSSoundDescriptorForm::SetFormEditorID to note the names the engine
    // does not keep.
    void InstallNameCapture();

    // kDataLoaded, game thread: indexes the sound descriptors (name, plugin)
    // and hooks BGSStandardSoundDef::DoResolve.
    void Install();

    struct Row
    {
        std::string   sound;         // editor ID, or "<plugin>|<local FormID>" without one
        std::string   plugin;        // file that defines the descriptor
        std::string   slider;        // label of the controlling slider; empty = none
        bool          extraPage{ false };  // that slider is on the "Extra Sliders" page
        float         secondsAgo{ 0.0F };
        std::uint32_t count{ 0 };    // requests while it was listed
    };

    // Render thread. Newest first; sounds leave the list two minutes after
    // their last request.
    [[nodiscard]] std::vector<Row> Snapshot();
}
