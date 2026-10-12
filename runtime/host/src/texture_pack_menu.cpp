// Import from Wind Waker HD, in the desktop menus' Mods tab (texture_pack_menu.h):
// picks the player's Wind Waker HD dump and runs the built-in importer
// (wwhd/import_job.h) in the background, with the disc BlueWake plays.
#include "texture_pack_menu.h"

#include "wwhd/import_job.h"

#include <imgui.h>

#include <cstdio>
#include <cstdlib>

namespace bw_texture_pack {

bool import_controls(SDL_Window* window, const std::string& textures_dir, std::string& folder) {
    static std::string message;
    static bool failed = false;
    wwhd::ImportJob& job = wwhd::import_job();

    std::string picked;
    if (take_folder(picked, Pick::wind_waker_hd)) {
        const char* disc = std::getenv("BLUEWAKE_DISC");
        message.clear();
        job.start(picked, disc != nullptr ? disc : "", textures_dir);
    }

    bool finished = false;
    wwhd::ImportJob::Status result;
    if (job.take_result(result)) {
        message = result.message;
        failed = result.failed;
        if (!result.failed) {
            folder = result.output;
            finished = true;
        }
    }

    const wwhd::ImportJob::Status now = job.status();
    if (now.running) {
        const float done = now.files_total ? float(now.files_read) / float(now.files_total) : 0.f;
        char label[96];
        std::snprintf(label, sizeof label, "%zu of %zu files", now.files_read, now.files_total);
        ImGui::ProgressBar(done, ImVec2(-1.f, 0.f), label);
        ImGui::TextDisabled("%s: %zu textures so far. You can close the menu and play; it carries on.",
                            now.message.c_str(), now.replacements);
        if (ImGui::SmallButton("Cancel the import"))
            job.cancel();
        return finished;
    }

    if (ImGui::Button("Import from Wind Waker HD..."))
        choose_folder(window, "", Pick::wind_waker_hd);
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
    ImGui::TextDisabled("Pick the folder of your own Wind Waker HD dump (the one with code, content and meta, or "
                        "its content folder). BlueWake makes an HD texture pack from it and the disc it plays.");
    if (!message.empty()) {
        if (failed)
            ImGui::TextColored(ImVec4(1.f, 0.6f, 0.4f, 1.f), "%s", message.c_str());
        else
            ImGui::TextUnformatted(message.c_str());
    }
    ImGui::PopTextWrapPos();
    return finished;
}

} // namespace bw_texture_pack
