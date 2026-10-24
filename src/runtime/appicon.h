// The programs' window icon on Linux: the PNG embedded at build time (tools/launcher/embedicon.cmake:
// build/icon/sa2b_icon.png when mkicon.py --disc has made it, else res/sa2b_icon.png). Windows takes
// the icon from the program's resources (res/sa2b.rc) instead. Include from one file per program.
#pragma once
#if !defined(_WIN32)

#include <SDL3/SDL.h>

namespace appicon {

#include "app_icon.inc"  // kAppIconPng, kAppIconPngSize

// The icon as RGBA (byte order R, G, B, A), or nullptr; the caller destroys it.
inline SDL_Surface* load() {
    SDL_IOStream* io = SDL_IOFromConstMem(kAppIconPng, kAppIconPngSize);
    SDL_Surface* png = io ? SDL_LoadPNG_IO(io, true) : nullptr;
    if (!png) return nullptr;
    SDL_Surface* rgba = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(png);
    return rgba;
}

}  // namespace appicon

#endif
