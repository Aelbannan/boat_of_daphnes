// Import from Wind Waker HD, in the desktop menus' Mods tab (texture_pack_menu.h):
// picks the player's Wind Waker HD dump and runs the built-in importer
// (wwhd/import_job.h) in the background, with the disc BlueWake plays.
#include "texture_pack_menu.h"

#include "wwhd/import_job.h"

#include <imgui.h>

#include <cstdio>
#include <cstdlib>

namespace bw_texture_pack {
namespace {

struct ImageImport {
    bool active = false;
    bool use_title = false;
    std::string image;
    wwhd::ImportKeyFiles keys;
};

void choose_file(SDL_Window* window, const std::string& start, Pick purpose) {
    // SDL retains filters until its asynchronous callback. Never use stack
    // filters or a callback that captures the form or an ImGui window.
    static const SDL_DialogFileFilter images[] = {{"Wii U disc images", "wud;wux"}};
    static const SDL_DialogFileFilter keys[] = {{"Key files", "key;bin;txt"}, {"All files", "*"}};
    if (begin_pick(purpose))
        SDL_ShowOpenFileDialog(picker_answer, nullptr, window,
                               purpose == Pick::disc_image ? images : keys,
                               purpose == Pick::disc_image ? 1 : 2,
                               start.empty() ? nullptr : start.c_str(), false);
}

std::string filename(const std::string& path) {
    // SDL paths are UTF-8, including on Windows; avoid a locale conversion.
    const size_t slash = path.find_last_of("/\\");
    return path.empty() ? "No file selected" : path.substr(slash == std::string::npos ? 0 : slash + 1);
}

void file_control(SDL_Window* window, const char* label, const std::string& path, Pick purpose) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    if (ImGui::SmallButton(path.empty() ? "Choose file..." : "Change file..."))
        choose_file(window, path, purpose);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", filename(path).c_str());
    ImGui::PopID();
}

} // namespace

bool import_controls(SDL_Window* window, const std::string& textures_dir, std::string& folder) {
    static std::string message;
    static bool failed = false;
    static ImageImport form;
    wwhd::ImportJob& job = wwhd::import_job();

    std::string picked;
    if (take_folder(picked, Pick::wind_waker_hd)) {
        const char* disc = std::getenv("BLUEWAKE_DISC");
        message.clear();
        failed = false;
        job.start(picked, disc != nullptr ? disc : "", textures_dir);
    }

    for (const Pick purpose : {Pick::disc_image, Pick::disc_key, Pick::common_key, Pick::title_key}) {
        std::string error;
        if (!take_pick(picked, error, purpose) || !form.active)
            continue;
        if (!error.empty()) {
            message = "The file picker could not open. Please try again.";
            failed = true;
        } else if (!picked.empty()) {
            message.clear();
            failed = false;
            if (purpose == Pick::disc_image) {
                const size_t dot = picked.find_last_of('.');
                std::string extension = dot == std::string::npos ? "" : picked.substr(dot);
                for (char& c : extension)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (extension != ".wud" && extension != ".wux") {
                    message = "Choose a WUD or WUX disc image.";
                    failed = true;
                } else if (picked != form.image) {
                    form.image = picked;
                    form.keys = {}; // A different disc needs its own key selection.
                }
            } else if (purpose == Pick::disc_key) {
                form.keys.disc = picked;
            } else if (purpose == Pick::title_key) {
                form.keys.title = picked;
            } else {
                form.keys.common = picked;
            }
        }
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

    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
    // Disable transitions until the current native dialog has answered. A
    // hidden/reopened menu can then safely consume its result on a later frame.
    ImGui::BeginDisabled(picker_busy());
    if (!form.active) {
        if (ImGui::Button("Import from Wind Waker HD..."))
            ImGui::OpenPopup("HD import source");
        if (ImGui::BeginPopup("HD import source")) {
            ImGui::TextUnformatted("Choose your Wind Waker HD input");
            if (ImGui::Selectable("Folder...")) {
                message.clear();
                failed = false;
                choose_folder(window, "", Pick::wind_waker_hd);
            }
            if (ImGui::Selectable("WUD/WUX image...")) {
                message.clear();
                failed = false;
                form = {};
                form.active = true;
                choose_file(window, "", Pick::disc_image);
            }
            ImGui::EndPopup();
        }
        ImGui::TextDisabled("Choose your Wind Waker HD dump folder (code, content and meta, or just content), "
                            "or a USA WUD/WUX image. BlueWake makes a texture pack using the GameCube disc it plays.");
    } else {
        file_control(window, "USA Wind Waker HD image", form.image, Pick::disc_image);
        file_control(window, "Disc key (required)", form.keys.disc, Pick::disc_key);
        ImGui::TextUnformatted("Use one of these to unlock the title:");
        if (ImGui::RadioButton("Common key", !form.use_title)) {
            form.use_title = false;
            form.keys.title.clear();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Title key", form.use_title)) {
            form.use_title = true;
            form.keys.common.clear();
        }
        file_control(window, form.use_title ? "Title key file" : "Common key file",
                     form.use_title ? form.keys.title : form.keys.common,
                     form.use_title ? Pick::title_key : Pick::common_key);
        ImGui::TextDisabled("Choose local key files. Their contents are never shown or saved in settings.");
        const bool ready = !form.image.empty() && !form.keys.disc.empty() &&
                           !(form.use_title ? form.keys.title : form.keys.common).empty();
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button("Import")) {
            const char* disc = std::getenv("BLUEWAKE_DISC");
            message.clear();
            failed = false;
            if (job.start(form.image, disc != nullptr ? disc : "", textures_dir, form.keys))
                form = {}; // start() copies the paths before the worker begins.
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            form = {};
            message.clear();
            failed = false;
        }
    }
    ImGui::EndDisabled();
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
