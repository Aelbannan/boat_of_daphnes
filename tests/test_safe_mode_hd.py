"""Compile the desktop startup/file paths without a game, renderer or Windows SDK.

The generated fixtures use production functions, with platform/UI calls stubbed.
Backup failure is injected; successful backups and settings writes use real files.
Run with: python3 tests/test_safe_mode_hd.py
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "runtime/host/src"
EFFECTS = ["DISTANT_ISLANDS", "BLOOM", "FAR_BLUR", "AO", "CLOUD_SHADOWS",
           "SUN_SHADOWS", "EDGE_LIGHT", "TORCH_LIGHT"]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


COMMON = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include "launch_marker.h"
#include "settings_state.h"
#include "smooth_rate.h"
#include "fps_position.h"
bool fail_backup;
bool fixture_backup(const char* path) { return !fail_backup && bw_launch_backup(path); }
#define bw_launch_backup fixture_backup
int bloom = -1, far_blur = -1, ao = -1;
void aurora_set_bloom(bool on) { bloom = on; }
void aurora_set_far_blur(bool on) { far_blur = on; }
void aurora_set_ambient_occlusion(bool on) { ao = on; }
std::string g_path, g_data_dir;
bool g_dirty, g_safe_mode;
std::string read_file(const std::string& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
'''

MAC = r'''
std::map<std::string, std::string> g_other;
std::string g_aspect;
bool g_betterww, g_safe_backed_up;
std::vector<std::pair<std::string, bool>> g_options;
std::vector<std::string> g_option_titles;
char g_texture_pack[1024];
using u32 = unsigned;
const char* bluewake_game_options_describe(u32, const char**, bool*, bool*) { return nullptr; }
void capture_launch_choices();
'''

WINDOWS = r'''
Settings g_saved, g_session, g_before_edit, g_launched;
unsigned long long g_dirty_at;
unsigned long long SDL_GetTicks() { return 1; }
int _putenv_s(const char* key, const char* value) {
    return *value ? setenv(key, value, 1) : unsetenv(key);
}
using DWORD = unsigned long;
constexpr int MAX_PATH = 260;
constexpr DWORD INVALID_FILE_ATTRIBUTES = ~0ul, FILE_ATTRIBUTE_DIRECTORY = 16;
DWORD GetModuleFileNameA(void*, char*, int) { return 0; }
DWORD GetFileAttributesA(const char*) { return INVALID_FILE_ATTRIBUTES; }
void CreateDirectoryA(const char*, void*) {}
void default_window(double, int* w, int* h) { *w = 640; *h = 480; }
bool on_a_monitor(int, int) { return true; }
void* game_window() { return nullptr; }
void apply_smooth_rate(void*) {}
void apply_fps_position() {}
void aurora_set_frame_interpolation(bool) {}
void aurora_set_fps_overlay(bool) {}
'''

CHECK_HD = r'''
void check_hd(int on, int bloom_on) {
    assert(bluewake_hd_distant_islands == bool(on));
    assert(bloom == bloom_on && far_blur == on && ao == on);
    assert(g_clouds == on && g_sun == on && g_edge == on && g_torch == on);
}
'''


class SafeModeHDTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="bluewake-safe-hd-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.build = Path(cls.temp.name)
        (cls.build / "aurora").mkdir()
        (cls.build / "aurora/aurora.h").write_text(
            "enum { FPS_OVERLAY_TOP_CENTER, FPS_OVERLAY_TOP_LEFT, FPS_OVERLAY_TOP_RIGHT, "
            "FPS_OVERLAY_BOTTOM_LEFT, FPS_OVERLAY_BOTTOM_RIGHT };\n")
        hd = (HOST / "hd_look.c").read_text()
        hd = hd[hd.index("bool bluewake_hd_distant_islands;"):hd.index("// GZLE01 revision 0.")]
        mac = (HOST / "settings_menu.cpp").read_text()
        win = (ROOT / "windows/src/win_settings.cpp").read_text()
        keys = mac[mac.index("const char* const kKeys[]"):mac.index("std::string g_path;")]
        mac_functions = ["std::string env(", "void set_env(", "std::string default_path(",
                         "std::string trim(", "bool managed(", "std::string chosen_mods(",
                         "std::string options_value(", "void save() {",
                         "void capture_launch_choices() {", "void apply_safe_mode() {",
                         'extern "C" void bluewake_settings_load(']
        win_functions = ["bool parse_bool(", "void load_file() {", "void save_file() {",
                         "void changed() {", "bool env_set(", "void env_default(",
                         "double aspect_ratio(", "std::string texture_folder(",
                         "std::string texture_pack_folder(", "const char* haptics_name(",
                         "void apply_hd_look() {", 'extern "C" void bw_settings_load(',
                         'extern "C" void bw_settings_apply_launch(']
        for platform in ("mac", "windows"):
            if platform == "mac":
                body = MAC + keys + "\n".join(function(mac, s) for s in mac_functions)
                launch = 'setenv("BLUEWAKE_SETTINGS", argv[1], 1); bluewake_settings_load();'
                input_path = 'argv[1]'
                enable = 'set_env("BLUEWAKE_HD_LOOK", "1"); bluewake_hd_look_reload();'
                disable = 'set_env("BLUEWAKE_HD_LOOK", "0"); bluewake_hd_look_reload();'
            else:
                body = WINDOWS + "\n".join(function(win, s) for s in win_functions)
                launch = 'bw_settings_load(argv[1]); bw_settings_apply_launch(); apply_hd_look();'
                input_path = 'std::string(argv[1]) + "settings.ini"'
                enable = 'g_session.hd_look = true; apply_hd_look();'
                disable = 'g_session.hd_look = false; apply_hd_look();'
            # Exercise the real Windows edit bookkeeping: another edit must not
            # save the session-only HD reset when the backup failed.
            windows_checks = r'''
                assert(g_saved.hd_look == (fail_backup || !safe));
                g_session.show_fps = true;
                changed();
                assert(g_saved.hd_look == (fail_backup || !safe));
                save_file();
            ''' if platform == "windows" else ""
            driver = r'''
            int main(int argc, char** argv) {
                assert(argc == 4);
                fail_backup = std::strcmp(argv[2], "failure") == 0;
                const bool safe = std::strcmp(argv[2], "normal") != 0;
                const std::string original = read_file(INPUT_PATH);
                LAUNCH
                if (!safe || fail_backup)
                    assert(read_file(g_path) == original);
                bluewake_hd_look_reload();
                check_hd(safe ? 0 : 1, safe ? 0 : std::atoi(argv[3]));
                WINDOWS_CHECKS
                if (safe) {
                    // The safe flag stays set; a later user choice must work.
                    ENABLE
                    check_hd(1, 1);
                    DISABLE
                    check_hd(0, 0);
                }
            }
            '''.replace("LAUNCH", launch).replace("WINDOWS_CHECKS", windows_checks)
            driver = driver.replace("INPUT_PATH", input_path)
            driver = driver.replace("ENABLE", enable).replace("DISABLE", disable)
            source = cls.build / (platform + ".cpp")
            source.write_text(COMMON + hd + body + CHECK_HD + driver)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-variable", "-I", str(cls.build), "-I", str(HOST),
                            "-I", str(ROOT / "windows/src"), str(source),
                            "-o", str(cls.build / platform)], check=True)

    def run_case(self, platform, mode, bloom):
        with tempfile.TemporaryDirectory(dir=self.build) as temp:
            directory = Path(temp)
            path = directory / "settings.ini"
            overrides = {"BLUEWAKE_HD_" + effect: "1" for effect in EFFECTS}
            overrides["BLUEWAKE_HD_BLOOM"] = str(bloom)
            if platform == "mac":
                original = "BLUEWAKE_HD_LOOK=1\n" + "".join(k + "=" + v + "\n" for k, v in overrides.items())
                original += "BLUEWAKE_HD_TRACE=1\nUNRELATED_SETTING=kept\n"
            else:
                original = "hd_look=1\nmouse_sensitivity=2.50\n"
            if mode != "none":
                path.write_text(original)
            env = {k: v for k, v in os.environ.items() if not k.startswith(("BLUEWAKE_", "DOL_"))}
            env.update(overrides, BLUEWAKE_HD_LOOK="1")
            if mode not in ("normal", "pending"):
                env["BLUEWAKE_SAFE_MODE"] = "1"
            if mode == "pending":
                (directory / "launch.pending").write_text("fixture\n")
            argument = "none" if mode == "none" else str(path)
            if platform == "windows":
                argument = str(directory) + "/"
            result = subprocess.run([str(self.build / platform), argument, mode, str(bloom)],
                                    env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            backups = list(directory.glob("settings.ini.before-safe-mode-*"))
            if mode in ("safe", "pending"):
                self.assertEqual(len(backups), 1)
                self.assertEqual(backups[0].read_text(), original)
                saved = path.read_text()
                self.assertIn("BLUEWAKE_HD_LOOK=0\n" if platform == "mac" else "hd_look=0\n", saved)
                if platform == "mac":
                    for key in overrides:
                        self.assertNotIn(key + "=", saved)
                    self.assertIn("UNRELATED_SETTING=kept\n", saved)
                    self.assertIn("BLUEWAKE_HD_TRACE=1\n", saved)
                else:
                    self.assertIn("mouse_sensitivity=2.50\n", saved)
            else:
                self.assertFalse(backups)
                if mode == "none":
                    self.assertFalse(path.exists())
                elif platform == "mac":
                    self.assertEqual(path.read_text(), original)
                else:
                    self.assertIn("hd_look=1\n", path.read_text())

    def test_mac_linux_recovery_and_normal_overrides(self):
        for mode in ("normal", "safe", "failure", "none"):
            for bloom in (0, 1):
                with self.subTest(mode=mode, bloom=bloom):
                    self.run_case("mac", mode, bloom)

    def test_windows_recovery_session_and_normal_overrides(self):
        for mode in ("normal", "safe", "failure", "pending"):
            for bloom in (0, 1):
                with self.subTest(mode=mode, bloom=bloom):
                    self.run_case("windows", mode, bloom)


if __name__ == "__main__":
    unittest.main()
