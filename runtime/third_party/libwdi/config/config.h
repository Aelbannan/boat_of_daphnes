/* libwdi's build settings for BlueWake: WinUSB only, x64 only. WDK_DIR (the Windows Kits folder of the machine that
   builds, passed by CMakeLists.txt) supplies the WinUSB co-installers libwdi's INF template names; they are
   Microsoft's redistributable files and are embedded at build time, never kept in this repository. */
#define COINSTALLER_DIR "wdf"
#define X64_DIR "x64"
#pragma once
#define WDF_VER 1011 /* only written into the INF's KMDF fields, which WinUSB ignores */
#define OPT_M64
#define ENABLE_LOGGING 1
#define INCLUDE_DEBUG_LOGGING
