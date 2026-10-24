// See gba_window.h.
#include "gba_window.h"
#include "ship_menu.h"
#include "ship/cvar.h"

#include <SDL3/SDL.h>
#include <aurora/imgui.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "gba_link.h"

extern "C" {
// runtime/host/src/main.c: the SI channel with the GBA, started and stopped live.
int bluewake_gba_set_mode(int mode);
int bluewake_gba_channel(void);
}

#define CVAR_SETTING(name) "gSettings." name

namespace gba_window {
namespace {

constexpr float kAspect = 240.0f / 160.0f;
constexpr const char* kModeCvar = CVAR_SETTING("TingleTuner.Mode");
constexpr const char* kShowCvar = CVAR_SETTING("TingleTuner.Window");      // 0 Auto, 1 Always, 2 Never
constexpr const char* kIntegerCvar = CVAR_SETTING("TingleTuner.IntegerScale");
constexpr const char* kVolumeCvar = CVAR_SETTING("TingleTuner.Volume");
constexpr const char* kMuteCvar = CVAR_SETTING("TingleTuner.Mute");

ImTextureID g_texture = 0;
std::vector<uint8_t> g_pixels(GBA_LINK_WIDTH * GBA_LINK_HEIGHT * 4, 0);
uint32_t g_seen_frame = 0;
bool g_have_frame = false;

uint64_t g_last_transfers = 0;
Uint64 g_last_activity = 0;  // SDL ticks of the last JoyBus transfer seen
std::string g_status = "Off";

SDL_AudioStream* g_audio = nullptr;
Uint64 g_audio_started = 0;
unsigned g_audio_rate = 0;

int mode() { return CVarGetInteger(kModeCvar, kInApp); }

// The window's size follows the content's 3:2 whatever edge is dragged.
void keep_aspect(ImGuiSizeCallbackData* data) {
    const float chrome = *static_cast<float*>(data->UserData);
    const bool integer = CVarGetInteger(kIntegerCvar, 0) != 0;
    float w = data->DesiredSize.x;
    float h = data->DesiredSize.y - chrome;
    const bool wider = std::fabs(data->DesiredSize.x - data->CurrentSize.x) >=
                       std::fabs((data->DesiredSize.y - chrome) - (data->CurrentSize.y - chrome)) * kAspect;
    if (wider)
        h = w / kAspect;
    else
        w = h * kAspect;
    if (integer) {
        const float k = std::max(1.0f, std::round(w / GBA_LINK_WIDTH));
        w = GBA_LINK_WIDTH * k;
        h = GBA_LINK_HEIGHT * k;
    }
    data->DesiredSize = ImVec2(w, h + chrome);
}

void feed_audio() {
    const bool wanted = mode() == kInApp && gba_link_running() && CVarGetInteger(kMuteCvar, 0) == 0;
    if (!wanted) {
        if (g_audio != nullptr) {
            SDL_DestroyAudioStream(g_audio);
            g_audio = nullptr;
            g_audio_rate = 0;
        }
        return;
    }
    const unsigned rate = gba_link_audio_rate();
    if (g_audio == nullptr) {
        SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(rate)};
        g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (g_audio == nullptr) {
            if (SDL_GetTicks() - g_audio_started > 5000) {
                g_audio_started = SDL_GetTicks();
                std::fprintf(stderr, "[gba] no audio stream: %s\n", SDL_GetError());
            }
            return;
        }
        SDL_ResumeAudioStreamDevice(g_audio);
        g_audio_rate = rate;
        std::fprintf(stderr, "[gba] audio stream open at %u Hz\n", rate);
    }
    if (rate != g_audio_rate) {
        // mGBA changes its output rate with the game's SOUNDBIAS resolution (32768, 65536, 131072 or 262144 Hz): the
        // voice samples (Tingle's "Hey!") run at a higher rate than the music. A stream left at the old rate plays
        // them slowed down by that ratio, a low pitch. The stream's source format follows the rate; SDL resamples.
        SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(rate)};
        SDL_SetAudioStreamFormat(g_audio, &spec, nullptr);
        std::fprintf(stderr, "[gba] audio rate changed %u -> %u Hz (played at the old rate it would be %.3fx pitch)\n",
                     g_audio_rate, rate, static_cast<double>(g_audio_rate) / rate);
        g_audio_rate = rate;
    }
    SDL_SetAudioStreamGain(g_audio, std::clamp(CVarGetInteger(kVolumeCvar, 60), 0, 100) / 100.0f);
    int16_t chunk[2048];
    for (;;) {
        // Never let more than about a quarter second queue up: a stalled device would only add delay.
        if (SDL_GetAudioStreamQueued(g_audio) > static_cast<int>(rate) * 4 / 4 * 1)
            break;
        const size_t n = gba_link_read_audio(chunk, 1024);
        if (n == 0)
            break;
        SDL_PutAudioStreamData(g_audio, chunk, static_cast<int>(n * 4));
    }
}

}  // namespace

void ApplyMode() {
    const int m = mode();
    bluewake_gba_set_mode(m == kInApp ? 1 : 0);
    g_status = m == kInApp ? "In-App GBA starting" : (m == kReal ? "Real GBA: needs a JoyBus bridge (not available)" : "Off");
}

void ApplySound() {}

const char* Status() {
    GbaLinkStats s{};
    gba_link_stats(&s);
    static char text[160];
    if (mode() == kReal) {
        return "Real GBA: a GameCube-to-GBA link needs a JoyBus bridge; the GameCube adapter cannot carry it";
    }
    if (!s.running)
        return mode() == kInApp ? "In-App GBA: waits for the Tingle Tuner (it starts the moment the save holds one)" : "Off";
    std::snprintf(text, sizeof text, "In-App GBA on port 2: %s, %llu JoyBus transfers, %llu frames",
                  s.joybus ? "ready (JoyBus connected)" : "starting up, the game can use it in a moment", static_cast<unsigned long long>(s.transfers),
                  static_cast<unsigned long long>(s.frames));
    return text;
}

void ResetGba() { gba_link_reset(); }

void UseDefaultKeys() {
    constexpr unsigned port = 1;
    struct Key {
        PADButton button;
        SDL_Scancode key;
    };
    static const Key keys[] = {
        {PAD_BUTTON_A, SDL_SCANCODE_X},        {PAD_BUTTON_B, SDL_SCANCODE_Z},
        {PAD_TRIGGER_Z, SDL_SCANCODE_RSHIFT},  {PAD_BUTTON_START, SDL_SCANCODE_RETURN},
        {PAD_BUTTON_UP, SDL_SCANCODE_UP},      {PAD_BUTTON_DOWN, SDL_SCANCODE_DOWN},
        {PAD_BUTTON_LEFT, SDL_SCANCODE_LEFT},  {PAD_BUTTON_RIGHT, SDL_SCANCODE_RIGHT},
        {PAD_TRIGGER_L, SDL_SCANCODE_A},       {PAD_TRIGGER_R, SDL_SCANCODE_S},
    };
    PADSetKeyboardActive(port, TRUE);
    for (const Key& k : keys)
        PADSetKeyButtonBinding(port, PADKeyButtonBinding{static_cast<s32>(k.key), k.button});
    CVarSetInteger(CVAR_SETTING("Controls.Port2.Keyboard"), 1);
    PADSerializeMappings();
    CVarSaveNextFrame();
    std::fprintf(stderr, "[gba] port 2 keyboard set to the Game Boy Advance layout\n");
}

void Frame(float scale, float fontScale) {
    feed_audio();
    if (mode() != kInApp || !gba_link_running())
        return;

    GbaLinkStats stats{};
    gba_link_stats(&stats);
    const Uint64 now = SDL_GetTicks();
    if (stats.transfers != g_last_transfers && stats.transfers > 40) {
        // A GBA being talked to beyond the probes: the game is booting or running the Tuner on it.
        g_last_transfers = stats.transfers;
        g_last_activity = now;
    }

    // The newest GBA frame into the texture.
    if (g_texture == 0)
        g_texture = aurora_imgui_create_dynamic_texture(GBA_LINK_WIDTH, GBA_LINK_HEIGHT);
    const uint32_t seq = gba_link_copy_frame(g_pixels.data(), g_seen_frame);
    if (seq != 0 && seq != g_seen_frame) {
        g_seen_frame = seq;
        g_have_frame = true;
        if (g_texture != 0)
            aurora_imgui_update_texture(g_texture, g_pixels.data());
    }

    const int show = CVarGetInteger(kShowCvar, 0);
    const bool recently_active = g_last_activity != 0 && now - g_last_activity < 6000;
    if (show == 2 || (show == 0 && !recently_active) || g_texture == 0 || !g_have_frame)
        return;

    float chrome = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSizeConstraints(ImVec2(GBA_LINK_WIDTH * 0.5f * scale, GBA_LINK_HEIGHT * 0.5f * scale + chrome),
                                        ImVec2(FLT_MAX, FLT_MAX), keep_aspect, &chrome);
    ImGui::SetNextWindowSize(ImVec2(480.0f * scale, 320.0f * scale + chrome), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20.0f * scale, 20.0f * scale), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    bool open = true;
    if (ImGui::Begin("Game Boy Advance (Tingle Tuner)###gba", &open,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse)) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float s = std::min(avail.x / GBA_LINK_WIDTH, avail.y / GBA_LINK_HEIGHT);
        if (CVarGetInteger(kIntegerCvar, 0) != 0)
            s = std::max(1.0f, std::floor(s));
        const ImVec2 size(GBA_LINK_WIDTH * s, GBA_LINK_HEIGHT * s);
        const ImVec2 origin = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(origin.x + (avail.x - size.x) * 0.5f, origin.y + (avail.y - size.y) * 0.5f));
        ImGui::Image(g_texture, size);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open) {
        // Closed with the X: hidden until the menu turns it back on.
        CVarSetInteger(kShowCvar, 2);
        CVarSaveNextFrame();
    }
    (void)fontScale;
}

}  // namespace gba_window
