#ifndef AURORA_IMGUI_H
#define AURORA_IMGUI_H

#include <imgui.h>

#ifdef __cplusplus
#include <cstdint>

extern "C" {
#else
#include "stdint.h"
#endif

ImTextureID aurora_imgui_add_texture(uint32_t width, uint32_t height, const void* rgba8);
// A texture whose pixels the caller replaces (an emulated screen): create it once, then update it as often as
// needed from any thread. rgba8 is width*height*4 bytes, replaced whole. The upload is queued on the render worker
// ahead of the frame that draws it.
ImTextureID aurora_imgui_create_dynamic_texture(uint32_t width, uint32_t height);
void aurora_imgui_update_texture(ImTextureID id, const void* rgba8);

#ifdef __cplusplus
}
#endif

#endif
