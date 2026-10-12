// The "Wind Waker HD textures" section of the desktop options menus: the Mac
// and Linux one (settings_menu.cpp) and the Windows one (win_settings.cpp).
// Both draw the section in their Mods tab, in the same order as the iPhone and
// iPad Mods menu (apple/ios/src/BWGameOverlay.mm), and count a pack the same way.
#pragma once

#include <SDL3/SDL.h>

#include <cctype>
#include <cstddef>
// The Windows host's POSIX shim (windows/compat/bw_posix_compat.h) defines
// rename() as a macro, which breaks <filesystem>'s own rename; set it aside here.
#pragma push_macro("rename")
#undef rename
#include <filesystem>
#pragma pop_macro("rename")
#include <mutex>
#include <string>
#include <system_error>

namespace bw_texture_pack {

// The replacement textures in a Dolphin-format pack: its .png and .dds files,
// leaving out the extra mip levels (name_mip1.png and so on), which belong to
// the texture before them.
inline size_t count(const std::string& folder) {
    namespace fs = std::filesystem;
    if (folder.empty())
        return 0;
    std::error_code ec;
    size_t textures = 0;
    fs::recursive_directory_iterator it(fs::path(folder), fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        std::error_code type;
        if (!it->is_regular_file(type))
            continue;
        std::string ext = it->path().extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if ((ext == ".png" || ext == ".dds") && it->path().stem().string().find("_mip") == std::string::npos)
            textures++;
    }
    return textures;
}

// count(), done again only when the folder changes or forget() is called (a
// pack holds thousands of files, and the menu draws every frame).
class Counter {
  public:
    size_t get(const std::string& folder) {
        if (!known_ || folder != folder_) {
            folder_ = folder;
            textures_ = count(folder);
            known_ = true;
        }
        return textures_;
    }
    void forget() { known_ = false; }

  private:
    std::string folder_;
    size_t textures_ = 0;
    bool known_ = false;
};

// The system's folder picker (SDL_ShowOpenFolderDialog). SDL may answer on
// another thread, so the answer waits here for the menu's next frame.
struct Picker {
    std::mutex lock;
    bool showing = false;
    bool answered = false;
    std::string folder; // the folder picked, when answered
    std::string error;  // why the picker could not open ("" when it did)
};

inline Picker& picker() {
    static Picker p;
    return p;
}

inline void choose_folder(SDL_Window* window, const std::string& start) {
    Picker& p = picker();
    {
        std::lock_guard<std::mutex> hold(p.lock);
        if (p.showing)
            return;
        p.showing = true;
        p.error.clear();
    }
    const auto answer = [](void*, const char* const* files, int) {
        Picker& p = picker();
        std::lock_guard<std::mutex> hold(p.lock);
        p.showing = false;
        if (files == nullptr) {
            p.error = SDL_GetError();
            if (p.error.empty())
                p.error = "this system has no folder picker";
        } else if (files[0] != nullptr) {
            p.folder = files[0];
            p.answered = true;
        }
    };
    SDL_ShowOpenFolderDialog(answer, nullptr, window, start.empty() ? nullptr : start.c_str(), false);
}

// The folder the player picked since the last call, if any.
inline bool take_folder(std::string& folder) {
    Picker& p = picker();
    std::lock_guard<std::mutex> hold(p.lock);
    if (!p.answered)
        return false;
    p.answered = false;
    folder = p.folder;
    return true;
}

// Why the picker could not open, or "".
inline std::string picker_error() {
    Picker& p = picker();
    std::lock_guard<std::mutex> hold(p.lock);
    return p.error;
}

} // namespace bw_texture_pack
