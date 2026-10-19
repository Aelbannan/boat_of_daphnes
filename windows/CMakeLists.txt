# Boat of Daphnes for Windows and Linux (x86-64): the unchanged host, GXRuntime/Aurora (Dawn on
# Direct3D 12 or Vulkan, SDL3) and the donor DSP. The one source list builds both. What differs
# is the entry shim (which fills in default paths beside the executable), the disc chooser, the
# crash reporter and the platform layer (src/platform: processes, HTTP, WebSocket, clipboard...);
# each has a Windows and a Linux file. On Windows the POSIX compatibility layer in compat/ stands
# in for what the host takes from the C library.
#
# Windows: built by scripts/windows/build.py with clang (GNU driver, MSVC ABI); see docs/WINDOWS.md.
# Linux: built by .github/workflows/linux.yml with clang and libstdc++ (cmake -S windows).
cmake_minimum_required(VERSION 3.25)
project(bluewake_windows LANGUAGES C CXX)
if(NOT WIN32 AND (NOT UNIX OR APPLE))
    message(FATAL_ERROR "windows/ builds the app for Windows and Linux only")
endif()
if(WIN32)
    # Every executable and DLL names its debug file without the folder it was built in (lld-link's %_PDB%), so no
    # build path is written into a release.
    add_link_options("LINKER:/PDBALTPATH:%_PDB%")
endif()

get_filename_component(BLUEWAKE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
# The runtime: vendored in runtime/recompcore (runtime/VENDORED.md), or another
# copy for a build beside the usual one.
set(BLUEWAKE_RECOMPCORE ${BLUEWAKE_REPO_ROOT}/runtime/recompcore CACHE PATH "The RecompCore tree to build with")
set(GXRUNTIME_DIR ${BLUEWAKE_RECOMPCORE}/GXRuntime)
set(BLUEWAKE_HOST_SRC ${BLUEWAKE_REPO_ROOT}/runtime/host/src)
set(BLUEWAKE_COMPAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/compat)

option(BLUEWAKE_WINDOWS_CONSOLE "Build a console-subsystem executable (logs on the terminal)" OFF)
option(BLUEWAKE_WINDOWS_DEVELOPER_TRACING "Compile BLUEWAKE_TRACE_* probes into the host" OFF)
option(BLUEWAKE_WINDOWS_REGRESSION_TESTS "Build source-only Windows host regressions" OFF)

# Source paths compiled into the app (assert and log messages) are written
# relative to the checkout, so a build does not carry the builder's home folder.
add_compile_options("-ffile-prefix-map=${BLUEWAKE_REPO_ROOT}/=")
if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE _USE_MATH_DEFINES _WIN32_WINNT=0x0A00)
else()
    add_compile_definitions(_USE_MATH_DEFINES $<$<COMPILE_LANGUAGE:C>:_GNU_SOURCE>)
endif()

add_subdirectory(${GXRUNTIME_DIR} gxruntime_build EXCLUDE_FROM_ALL)

# POSIX calls the host makes (threads, clocks, dlopen, dirent): force-included
# into BlueWake's own sources only, never into third-party code.
if(WIN32)
    add_library(bluewake_posix_compat STATIC ${BLUEWAKE_COMPAT_DIR}/bw_posix_compat.c)
    target_include_directories(bluewake_posix_compat PRIVATE ${BLUEWAKE_COMPAT_DIR})
    set(BLUEWAKE_COMPAT_INCLUDES ${BLUEWAKE_COMPAT_DIR}/include ${BLUEWAKE_COMPAT_DIR})
    set(BLUEWAKE_COMPAT_FORCE_INCLUDE "SHELL:-include ${BLUEWAKE_COMPAT_DIR}/bw_posix_compat.h")
    # GXRuntime's own C runtime (the disc reader uses fseeko and off_t) needs the
    # same few declarations, and remembers its getenv answers (the Windows C
    # runtime's getenv is slow on hot paths); its C++ graphics code builds
    # unchanged.
    target_compile_options(gxruntime PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE}
        "SHELL:-include ${BLUEWAKE_COMPAT_DIR}/bw_getenv_cache.h")
else()
    # Linux has all of it: the library is empty and nothing is force-included, so the lines below that name them work as is.
    add_library(bluewake_posix_compat INTERFACE)
    set(BLUEWAKE_COMPAT_INCLUDES "")
    set(BLUEWAKE_COMPAT_FORCE_INCLUDE "")
endif()

# Archive reading for mods (.zip, .rar, .7z): libarchive, vendored in runtime/third_party/libarchive
# (VENDORED.md), over Aurora's zlib and RecompCore's liblzma. That one is Dolphin's own CMake: it
# expects Dolphin's warning macro, and its command-line tool helpers (tuklib's terminal width, exit and
# program name code) are left out because the library never calls them.
function(dolphin_disable_warnings target)
    target_compile_options(${target} PRIVATE -w)
endfunction()
add_subdirectory(${BLUEWAKE_RECOMPCORE}/Externals/liblzma liblzma_build EXCLUDE_FROM_ALL)
get_target_property(_lzma_sources lzma SOURCES)
list(FILTER _lzma_sources EXCLUDE REGEX "tuklib_(mbstr_fw|mbstr_width|exit|open_stdxxx|progname)[.]c$")
set_property(TARGET lzma PROPERTY SOURCES ${_lzma_sources})
add_subdirectory(${BLUEWAKE_REPO_ROOT}/runtime/third_party/libarchive libarchive_build EXCLUDE_FROM_ALL)
# Compressed disc images (.rvz, .wia, .gcz, .ciso) unpacked natively: bzip2 and Zstandard decoders, vendored; zlib and liblzma as above.
add_subdirectory(${BLUEWAKE_REPO_ROOT}/runtime/third_party/bzip2 bzip2_build EXCLUDE_FROM_ALL)
add_subdirectory(${BLUEWAKE_REPO_ROOT}/runtime/third_party/zstd zstd_build EXCLUDE_FROM_ALL)
if(WIN32)
    # WinUSB driver install for the GameCube adapter: libwdi (LGPL-3.0), vendored in runtime/third_party/libwdi.
    add_subdirectory(${BLUEWAKE_REPO_ROOT}/runtime/third_party/libwdi libwdi_build EXCLUDE_FROM_ALL)
endif()

# The Tingle Tuner's Game Boy Advance: mGBA (MPL-2.0), vendored in runtime/third_party/mgba (VENDORED.md), as the
# library only (no frontends, no zlib/lzma/png), the way Dolphin builds it. Only the GBA core is wanted.
set(LIBMGBA_ONLY ON)
set(USE_LZMA OFF)
set(M_CORE_GB OFF)
set(M_CORE_GBA ON)
add_subdirectory(${BLUEWAKE_REPO_ROOT}/runtime/third_party/mgba mgba_build EXCLUDE_FROM_ALL)
target_compile_definitions(mgba PUBLIC ENABLE_VFS ENABLE_DIRECTORIES M_CORE_GBA)
target_compile_options(mgba PRIVATE -w)
if(WIN32)
    # mGBA links libm by name unless the compiler is MSVC; this clang is not (the C runtime has the math functions).
    get_target_property(_mgba_libs mgba LINK_LIBRARIES)
    list(REMOVE_ITEM _mgba_libs m)
    set_property(TARGET mgba PROPERTY LINK_LIBRARIES "${_mgba_libs}")
    get_target_property(_mgba_ilibs mgba INTERFACE_LINK_LIBRARIES)
    list(REMOVE_ITEM _mgba_ilibs m)
    set_property(TARGET mgba PROPERTY INTERFACE_LINK_LIBRARIES "${_mgba_ilibs}")
endif()

# emibios (LGPL-3.0-or-later, runtime/third_party/emibios): the free GBA BIOS, compiled in as a C array.
file(READ ${BLUEWAKE_REPO_ROOT}/runtime/third_party/emibios/gba_bios.bin _emibios_hex HEX)
string(LENGTH "${_emibios_hex}" _emibios_hex_len)
math(EXPR _emibios_size "${_emibios_hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _emibios_c "${_emibios_hex}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/emibios_data.c.tmp
    "/* generated from runtime/third_party/emibios/gba_bios.bin */
const unsigned char bluewake_emibios[] = {${_emibios_c}};
const unsigned bluewake_emibios_size = ${_emibios_size}u;
")
file(COPY_FILE ${CMAKE_CURRENT_BINARY_DIR}/emibios_data.c.tmp ${CMAKE_CURRENT_BINARY_DIR}/emibios_data.c ONLY_IF_DIFFERENT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BLUEWAKE_REPO_ROOT}/runtime/third_party/emibios/gba_bios.bin)

set(BLUEWAKE_HOST_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/emibios_data.c
    ${BLUEWAKE_HOST_SRC}/main.c
    ${BLUEWAKE_HOST_SRC}/aram_dma.c
    ${BLUEWAKE_HOST_SRC}/audio_capture.c
    ${BLUEWAKE_HOST_SRC}/ipl_sram.c
    ${BLUEWAKE_HOST_SRC}/card_runtime.c
    ${BLUEWAKE_HOST_SRC}/callback_delivery.c
    ${BLUEWAKE_HOST_SRC}/cycle_domain.c
    ${BLUEWAKE_HOST_SRC}/delivery_digest.c
    ${BLUEWAKE_HOST_SRC}/edge_intercepts.c
    ${BLUEWAKE_HOST_SRC}/return_census.c
    ${BLUEWAKE_HOST_SRC}/interrupt_sources.c
    ${BLUEWAKE_HOST_SRC}/cold_fallback.c
    ${BLUEWAKE_HOST_SRC}/external_memory.c
    ${BLUEWAKE_HOST_SRC}/fpu_context.c
    ${BLUEWAKE_HOST_SRC}/pad_event_schedule.c
    ${BLUEWAKE_HOST_SRC}/pad_waypoint.c
    ${BLUEWAKE_HOST_SRC}/pad_wire.c
    ${BLUEWAKE_HOST_SRC}/gc_adapter.c
    ${BLUEWAKE_HOST_SRC}/gc_adapter_proto.c
    ${BLUEWAKE_HOST_SRC}/rel_scratch_allocator.c
    ${BLUEWAKE_HOST_SRC}/scheduler_contract.c
    ${BLUEWAKE_HOST_SRC}/fast_load.c
    ${BLUEWAKE_HOST_SRC}/save_state.c
    ${BLUEWAKE_HOST_SRC}/quick_doors.c
    ${BLUEWAKE_HOST_SRC}/haptics.c
    ${BLUEWAKE_HOST_SRC}/draw_tags.c
    ${BLUEWAKE_HOST_SRC}/forest_water.c
    ${BLUEWAKE_HOST_SRC}/audio_watch.c
    ${BLUEWAKE_HOST_SRC}/voice_pitch.c
    ${BLUEWAKE_HOST_SRC}/audio_volume.c
    ${BLUEWAKE_HOST_SRC}/autosave.c
    ${BLUEWAKE_HOST_SRC}/fps_watch.c
    ${BLUEWAKE_HOST_SRC}/prof.c
    ${BLUEWAKE_HOST_SRC}/game_options.c
    ${BLUEWAKE_HOST_SRC}/rando_seed.c
    ${BLUEWAKE_HOST_SRC}/rando_host.c
    ${BLUEWAKE_HOST_SRC}/rando_ui.c
    ${BLUEWAKE_HOST_SRC}/ui_pad.c
    ${BLUEWAKE_HOST_SRC}/host_input.c
    ${BLUEWAKE_HOST_SRC}/widescreen.c
    ${BLUEWAKE_HOST_SRC}/test_control.c
    ${BLUEWAKE_HOST_SRC}/treasure_glow.c
    ${BLUEWAKE_HOST_SRC}/player_model.c
    ${BLUEWAKE_HOST_SRC}/guest_coro.c
    ${BLUEWAKE_HOST_SRC}/puppet.c
    ${BLUEWAKE_HOST_SRC}/puppet_pvp.c
    ${BLUEWAKE_HOST_SRC}/bunny_hood.c
    ${BLUEWAKE_HOST_SRC}/text_swap.c
    ${BLUEWAKE_HOST_SRC}/cosmetics.c
    ${BLUEWAKE_HOST_SRC}/game_state.c
    ${BLUEWAKE_HOST_SRC}/jabun_guard.c
    ${BLUEWAKE_HOST_SRC}/dev_tools.c
    ${BLUEWAKE_HOST_SRC}/team_sync.c
    ${BLUEWAKE_HOST_SRC}/switch_sync.c
    ${BLUEWAKE_HOST_SRC}/mod_archive.c
    ${BLUEWAKE_HOST_SRC}/mods_host.c
    ${BLUEWAKE_HOST_SRC}/gba_link.c
    ${BLUEWAKE_HOST_SRC}/ship/cvar.cpp)

# The randomizer's host tables (scripts/rando/variant_sites.py writes rando_sites.inc in the build's rando folder); without
# them the seed code is compiled with no sites and the randomizer mod cannot play a seed.
set(BLUEWAKE_RANDO_SITES "" CACHE FILEPATH "Generated rando_sites.inc of the randomizer variant (empty: none)")
if(BLUEWAKE_RANDO_SITES)
    set_source_files_properties(${BLUEWAKE_HOST_SRC}/rando_seed.c PROPERTIES
        COMPILE_DEFINITIONS "BLUEWAKE_RANDO_SITES_INC=\"${BLUEWAKE_RANDO_SITES}\""
        OBJECT_DEPENDS "${BLUEWAKE_RANDO_SITES}")
endif()

# The entry shim, disc chooser, crash reporter and platform layer are per operating system. The Windows manifest
# (DPI awareness, long paths, UTF-8 code page) is merged into the one the linker embeds.
if(WIN32)
    set(BLUEWAKE_PLATFORM_SOURCES
        src/win_entry.c
        src/win_disc.c
        src/win_crash.cpp
        src/adapter_driver.cpp
        src/platform/platform_win32.cpp
        src/bluewake.rc
        resources/BlueWake.manifest)
else()
    set(BLUEWAKE_PLATFORM_SOURCES
        src/linux_entry.c
        src/linux_disc.c
        src/linux_crash.cpp
        src/platform/adapter_driver_linux.cpp
        src/platform/platform_posix.cpp
        src/platform/net_posix.cpp)
endif()
add_executable(BlueWake
    ${BLUEWAKE_PLATFORM_SOURCES}
    ${BLUEWAKE_HOST_SRC}/disc_import.c
    ${BLUEWAKE_HOST_SRC}/disc_unpack.c
    src/win_settings.cpp
    src/input_editor.cpp
    src/gba_window.cpp
    src/dev_windows.cpp
    src/ship_menu.cpp
    src/cosmetics_editor.cpp
    src/link_colors.cpp
    src/model_colors.cpp
    src/gx_texture.cpp
    src/png_decode.cpp
    src/anchor.cpp
    src/anchor_client.cpp
    src/anchor_content.cpp
    src/item_found.cpp
    src/notifications.cpp
    src/item_tracker.cpp
    src/check_tracker.cpp
    src/input_viewer.cpp
    src/mods.cpp
    src/rarc.cpp
    src/hud_colors.cpp
    src/randomizer_menu.cpp
    src/rando_pack.cpp
    src/rando_saves.cpp
    src/rando_flow.cpp
    src/fs_art.cpp
    src/fs_blo.cpp
    src/fs_font.cpp
    ${BLUEWAKE_HOST_SOURCES})
# The unchanged host keeps its own entry point under another name; the shim
# owns main() and hands over after setting the defaults.
set_source_files_properties(${BLUEWAKE_HOST_SRC}/main.c PROPERTIES
    COMPILE_DEFINITIONS "main=bluewake_host_main")
target_link_libraries(BlueWake PRIVATE bluewake_bzip2 bluewake_zstd ZLIB::ZLIB LibLZMA::LibLZMA mgba gxruntime GXRuntime::aurora xxhash bluewake_posix_compat bluewake_archive)
if(WIN32)
    target_link_libraries(BlueWake PRIVATE winmm bcrypt comctl32 comdlg32 winhttp setupapi)
    if(WDI_AVAILABLE)
        target_link_libraries(BlueWake PRIVATE wdi)
        target_compile_definitions(BlueWake PRIVATE BLUEWAKE_HAS_LIBWDI=1)
    endif()
else()
    # The platform layer's sockets and TLS (Anchor, content transfers) use the system's OpenSSL; the module is loaded with dlopen.
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(BlueWake PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
if(TARGET imgui)
    target_link_libraries(BlueWake PRIVATE imgui)
endif()
set_target_properties(BlueWake PROPERTIES LINKER_LANGUAGE CXX OUTPUT_NAME boat-of-daphnes)
target_include_directories(BlueWake PRIVATE
    ${BLUEWAKE_COMPAT_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${BLUEWAKE_HOST_SRC}
    ${GXRUNTIME_DIR}/include
    ${BLUEWAKE_RECOMPCORE}/Externals/picojson
    ${BLUEWAKE_RECOMPCORE}/Source/Core/Core/PowerPC/StaticRecomp)
if(BLUEWAKE_COMPAT_FORCE_INCLUDE)
    target_compile_options(BlueWake PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:${BLUEWAKE_COMPAT_FORCE_INCLUDE}>)
endif()
# A function that only calls itself is always a mistake here (a wrapper that forgot its real call hung the game).
target_compile_options(BlueWake PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:-Werror=infinite-recursion>)
target_compile_definitions(BlueWake PRIVATE
    DOLRECOMP_CPU_HEADER="core/cpu.h"
    BLUEWAKE_ENABLE_DEVELOPER_TRACING=$<BOOL:${BLUEWAKE_WINDOWS_DEVELOPER_TRACING}>
    BLUEWAKE_EDGE_CENSUS=0
    BLUEWAKE_APP_NAME="Boat-Of-Daphnes")
if(WIN32)
    target_compile_definitions(BlueWake PRIVATE BLUEWAKE_WINDOWS=1)
    # The game's translated code recurses on the host stack as the guest does on
    # the GameCube's; macOS gives the main thread 8 MB, Windows only 1 MB.
    target_link_options(BlueWake PRIVATE "LINKER:/STACK:67108864")
    if(BLUEWAKE_WINDOWS_CONSOLE)
        target_compile_definitions(BlueWake PRIVATE BLUEWAKE_WINDOWS_CONSOLE=1)
    else()
        # A windowed game: no console of its own. It still prints to the terminal
        # it was started from, and always to the session log (src/win_entry.c).
        target_link_options(BlueWake PRIVATE "LINKER:/SUBSYSTEM:WINDOWS" "LINKER:/ENTRY:mainCRTStartup")
    endif()
else()
    # Linux's main thread stack grows to its limit, which src/linux_entry.c raises to 64 MB.
    target_compile_definitions(BlueWake PRIVATE BLUEWAKE_LINUX=1)
endif()

# The donor DSP (Dolphin's DSP interpreter and its high-level Zelda ucode, from
# RecompCore) compiled from source, with only the Common services it needs
# (dsp/dsp_common_shim.cpp).
set(_donor ${BLUEWAKE_RECOMPCORE})
set(_dsp ${_donor}/Source/Core/Core/DSP)
add_library(bluewake_dsp_donor STATIC
    ${_dsp}/DSPAccelerator.cpp ${_dsp}/DSPAnalyzer.cpp ${_dsp}/DSPAssembler.cpp
    ${_dsp}/DSPCaptureLogger.cpp ${_dsp}/DSPCodeUtil.cpp ${_dsp}/DSPCore.cpp
    ${_dsp}/DSPDisassembler.cpp ${_dsp}/DSPHWInterface.cpp ${_dsp}/DSPMemoryMap.cpp
    ${_dsp}/DSPStacks.cpp ${_dsp}/DSPTables.cpp ${_dsp}/LabelMap.cpp
    ${_dsp}/Interpreter/DSPIntArithmetic.cpp ${_dsp}/Interpreter/DSPIntBranch.cpp
    ${_dsp}/Interpreter/DSPIntExtOps.cpp ${_dsp}/Interpreter/DSPIntLoadStore.cpp
    ${_dsp}/Interpreter/DSPIntMisc.cpp ${_dsp}/Interpreter/DSPIntMultiplier.cpp
    ${_dsp}/Interpreter/DSPIntTables.cpp ${_dsp}/Interpreter/DSPInterpreter.cpp
    ${_dsp}/Jit/DSPEmitterBase.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/DSPHLE.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/MailHandler.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/AESnd.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/ASnd.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/AX.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/AXWii.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/CARD.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/GBA.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/INIT.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/ROM.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/UCodes.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/Zelda.cpp
    ${_donor}/Source/Core/Core/HW/DSPHLE/UCodes/ZeldaUCodesTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/dsp_common_shim.cpp)
target_include_directories(bluewake_dsp_donor PUBLIC
    ${_donor}/Source/Core ${_donor}/Source ${_donor}/Externals
    ${_donor}/Externals/xxhash/xxHash
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp/generated)
# No _M_X86_64: that selects Dolphin's x64 DSP JIT, and BlueWake runs the DSP
# interpreter everywhere (DSPEmitterNull).
target_compile_definitions(bluewake_dsp_donor PUBLIC
    _ARCH_64=1 FMT_EXCEPTIONS=0
    __STDC_CONSTANT_MACROS __STDC_LIMIT_MACROS)
if(WIN32)
    target_compile_definitions(bluewake_dsp_donor PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()
target_compile_features(bluewake_dsp_donor PUBLIC cxx_std_23)
target_compile_options(bluewake_dsp_donor PRIVATE -fno-strict-aliasing -w)
# Dolphin builds with a conforming preprocessor (MSVC /Zc:preprocessor); its
# assert macros forward __VA_ARGS__ through __VA_OPT__, which clang's MSVC
# compatibility mode expands the old MSVC way, miscounting format arguments.
if(WIN32)
    target_compile_options(bluewake_dsp_donor PRIVATE -fno-ms-compatibility)
endif()
# Under the Microsoft ABI a member pointer's size depends on whether its class
# is complete where the pointer type is first used; a forward-declared one gave
# the DSP interpreter's op table two layouts and Exact audio a call to address 0
# (patches/recompcore/0114). Refuse such pointers in the donor DSP.
if(WIN32)
    target_compile_options(bluewake_dsp_donor PRIVATE -fcomplete-member-pointers)
endif()
target_link_libraries(bluewake_dsp_donor PUBLIC fmt::fmt xxhash ZLIB::ZLIB)

add_library(bluewake_dsp_adapter STATIC
    ${BLUEWAKE_HOST_SRC}/dsp_adapter.cpp
    ${BLUEWAKE_HOST_SRC}/dsp_adapter_c.cpp
    ${BLUEWAKE_HOST_SRC}/dsp_adapter_donor_stubs.cpp
    ${BLUEWAKE_HOST_SRC}/dsp_hle_backend.cpp)
target_include_directories(bluewake_dsp_adapter PRIVATE ${BLUEWAKE_COMPAT_INCLUDES})
if(WIN32)
    target_compile_options(bluewake_dsp_adapter PRIVATE -fno-ms-compatibility)
endif()
target_link_libraries(bluewake_dsp_adapter PUBLIC bluewake_dsp_donor bluewake_posix_compat)
target_compile_definitions(BlueWake PRIVATE BLUEWAKE_HAS_DSP_ADAPTER=1)
target_link_libraries(BlueWake PRIVATE bluewake_dsp_adapter)

# Tools the builder runs on the player's disc.
add_executable(bluewake_disc_extract
    ${BLUEWAKE_REPO_ROOT}/scripts/windows/disc_extract.c
    ${BLUEWAKE_HOST_SRC}/disc_import.c)
target_include_directories(bluewake_disc_extract PRIVATE
    ${BLUEWAKE_COMPAT_INCLUDES} ${BLUEWAKE_HOST_SRC})
target_compile_options(bluewake_disc_extract PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
target_link_libraries(bluewake_disc_extract PRIVATE bluewake_posix_compat)
if(WIN32)
    target_link_libraries(bluewake_disc_extract PRIVATE bcrypt)
endif()

# The disc unpacker as a command line, for testing it on images: bluewake_disc_unpack IN OUT. Not part of the default build:
# python scripts/windows/build_app_only.py --target bluewake_disc_unpack.
add_executable(bluewake_disc_unpack EXCLUDE_FROM_ALL
    ${BLUEWAKE_REPO_ROOT}/tests/disc_unpack_cli.c
    ${BLUEWAKE_HOST_SRC}/disc_unpack.c
    ${BLUEWAKE_HOST_SRC}/disc_import.c)
target_include_directories(bluewake_disc_unpack PRIVATE ${BLUEWAKE_COMPAT_INCLUDES} ${BLUEWAKE_HOST_SRC})
target_compile_options(bluewake_disc_unpack PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
target_link_libraries(bluewake_disc_unpack PRIVATE bluewake_bzip2 bluewake_zstd ZLIB::ZLIB LibLZMA::LibLZMA bluewake_posix_compat)
if(WIN32)
    target_link_libraries(bluewake_disc_unpack PRIVATE bcrypt)
endif()

# The GameCube adapter's protocol (reports, origin, rumble, replay files) replayed from reports byte by byte; no
# device. Defined always but not part of the default build: python scripts/windows/build_app_only.py --target
# bluewake_gc_adapter_proto_test, then run build/windows/app/bluewake_gc_adapter_proto_test.exe.
add_executable(bluewake_gc_adapter_proto_test EXCLUDE_FROM_ALL
    ${BLUEWAKE_REPO_ROOT}/tests/gc_adapter_proto_test.c ${BLUEWAKE_HOST_SRC}/gc_adapter_proto.c)
target_include_directories(bluewake_gc_adapter_proto_test PRIVATE ${BLUEWAKE_HOST_SRC})

if(BLUEWAKE_WINDOWS_REGRESSION_TESTS)
    # Run manually against two private own-disc modules; no game code in CI.
    add_executable(bluewake_fast_blocks_test EXCLUDE_FROM_ALL ${BLUEWAKE_REPO_ROOT}/tests/fast_blocks_test.c)
    target_include_directories(bluewake_fast_blocks_test PRIVATE
        ${GXRUNTIME_DIR}/include
        ${BLUEWAKE_RECOMPCORE}/Source/Core/Core/PowerPC/StaticRecomp)
    target_compile_definitions(bluewake_fast_blocks_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    # Assertion-based scheduler checks also run in Release configurations.
    add_executable(bluewake_cycle_domain_test
        ${BLUEWAKE_REPO_ROOT}/tests/cycle_domain_test.c ${BLUEWAKE_HOST_SRC}/cycle_domain.c)
    add_executable(bluewake_interrupt_sources_test
        ${BLUEWAKE_REPO_ROOT}/tests/interrupt_sources_test.c
        ${BLUEWAKE_HOST_SRC}/cycle_domain.c ${BLUEWAKE_HOST_SRC}/interrupt_sources.c)
    add_executable(bluewake_delivery_digest_test
        ${BLUEWAKE_REPO_ROOT}/tests/delivery_digest_test.c ${BLUEWAKE_HOST_SRC}/delivery_digest.c)
    add_executable(bluewake_composite_dispatch_loop_test
        ${BLUEWAKE_REPO_ROOT}/tests/composite_dispatch_loop_test.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/dispatch_loop.c)
    add_executable(bluewake_edge_intercepts_test
        ${BLUEWAKE_REPO_ROOT}/tests/edge_intercepts_test.c
        ${BLUEWAKE_HOST_SRC}/edge_intercepts.c ${BLUEWAKE_HOST_SRC}/game_options.c ${BLUEWAKE_HOST_SRC}/rando_seed.c ${BLUEWAKE_HOST_SRC}/card_runtime.c)
    foreach(TEST_NAME cycle_domain interrupt_sources delivery_digest composite_dispatch_loop edge_intercepts)
        set(TEST_TARGET bluewake_${TEST_NAME}_test)
        target_include_directories(${TEST_TARGET} PRIVATE
            ${BLUEWAKE_HOST_SRC} ${BLUEWAKE_REPO_ROOT}/cmake/composite
            ${GXRUNTIME_DIR}/include ${BLUEWAKE_RECOMPCORE}/Source/Core/Core/PowerPC/StaticRecomp
            ${BLUEWAKE_COMPAT_INCLUDES})
        target_compile_definitions(${TEST_TARGET} PRIVATE DOLRECOMP_CPU_HEADER="core/cpu.h")
        target_compile_options(${TEST_TARGET} PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
        target_link_libraries(${TEST_TARGET} PRIVATE gxruntime bluewake_posix_compat)
        set_target_properties(${TEST_TARGET} PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    endforeach()
    if(NOT WIN32)
        # edge_intercepts.c calls the fork's hooks (autosave, puppets, Bunny Hood...), which the test does not exercise and does
        # not link; lld on Windows leaves them out as unused, GNU-style linking wants them named.
        target_link_options(bluewake_edge_intercepts_test PRIVATE -Wl,--unresolved-symbols=ignore-all)
    endif()

    # Source-only contracts for helpers awaiting whole-module qualification.
    foreach(_variant plain global)
        add_executable(bluewake_gather_memory_${_variant}_test
            ${BLUEWAKE_REPO_ROOT}/tests/gather_memory_test.c
            ${BLUEWAKE_REPO_ROOT}/cmake/composite/gather_pipe.c)
        target_include_directories(bluewake_gather_memory_${_variant}_test PRIVATE
            ${BLUEWAKE_REPO_ROOT}/cmake/composite ${GXRUNTIME_DIR}/tests)
        target_link_libraries(bluewake_gather_memory_${_variant}_test PRIVATE gxruntime)
        if(_variant STREQUAL "global")
            target_compile_definitions(bluewake_gather_memory_${_variant}_test PRIVATE
                BW_GUEST_MEM1=bw_test_mem1 BW_GUEST_MEM1_SIZE=0x02000000u)
        endif()
        add_test(NAME bluewake_gather_memory_${_variant}_test COMMAND bluewake_gather_memory_${_variant}_test)
    endforeach()
    add_executable(bluewake_inline_gpr_memory_test ${BLUEWAKE_REPO_ROOT}/tests/inline_gpr_memory_test.c)
    target_include_directories(bluewake_inline_gpr_memory_test PRIVATE
        ${GXRUNTIME_DIR}/include ${BLUEWAKE_REPO_ROOT}/cmake/composite)
    target_link_libraries(bluewake_inline_gpr_memory_test PRIVATE gxruntime)
    add_test(NAME bluewake_inline_gpr_memory_test COMMAND bluewake_inline_gpr_memory_test)

    add_executable(bluewake_direct_calls_test
        ${BLUEWAKE_REPO_ROOT}/tests/direct_calls_test.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/direct_calls.c)
    target_include_directories(bluewake_direct_calls_test PRIVATE
        ${GXRUNTIME_DIR}/include ${BLUEWAKE_REPO_ROOT}/cmake/composite ${BLUEWAKE_REPO_ROOT}/tests)
    target_compile_definitions(bluewake_direct_calls_test PRIVATE
        BLUEWAKE_EDGE_FILTER=1 BW_EDGE_WATCH_INCLUDE="direct_call_watch.inc")
    add_test(NAME bluewake_direct_calls_test COMMAND bluewake_direct_calls_test)

    add_executable(bluewake_guest_checkpoint_test ${BLUEWAKE_REPO_ROOT}/tests/guest_checkpoint_test.c)
    target_include_directories(bluewake_guest_checkpoint_test PRIVATE
        ${GXRUNTIME_DIR}/include ${BLUEWAKE_REPO_ROOT}/runtime/host/src)
    target_link_libraries(bluewake_guest_checkpoint_test PRIVATE xxhash)
    add_test(NAME bluewake_guest_checkpoint_test COMMAND bluewake_guest_checkpoint_test)

    add_executable(bluewake_gather_host_test
        ${BLUEWAKE_REPO_ROOT}/tests/gather_host_test.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/gather_pipe.c)
    target_include_directories(bluewake_gather_host_test PRIVATE
        ${BLUEWAKE_REPO_ROOT}/cmake/composite ${BLUEWAKE_REPO_ROOT}/runtime/host/src)
    target_link_libraries(bluewake_gather_host_test PRIVATE gxruntime)
    add_test(NAME bluewake_gather_host_test COMMAND bluewake_gather_host_test)

    add_executable(bluewake_gather_dispatch_test
        ${BLUEWAKE_REPO_ROOT}/tests/gather_dispatch_test.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/gather_pipe.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/dispatch_loop.c)
    target_include_directories(bluewake_gather_dispatch_test PRIVATE ${BLUEWAKE_REPO_ROOT}/cmake/composite)
    target_link_libraries(bluewake_gather_dispatch_test PRIVATE gxruntime)
    target_compile_definitions(bluewake_gather_dispatch_test PRIVATE BLUEWAKE_GATHER_PIPE=1)
    add_test(NAME bluewake_gather_dispatch_test COMMAND bluewake_gather_dispatch_test)
    foreach(_test bluewake_inline_gpr_memory_test bluewake_direct_calls_test bluewake_guest_checkpoint_test bluewake_gather_host_test bluewake_gather_memory_plain_test bluewake_gather_memory_global_test bluewake_gather_dispatch_test)
        target_include_directories(${_test} PRIVATE ${BLUEWAKE_COMPAT_INCLUDES})
        target_compile_options(${_test} PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
        target_link_libraries(${_test} PRIVATE bluewake_posix_compat)
        set_target_properties(${_test} PROPERTIES LINKER_LANGUAGE CXX)
    endforeach()

    add_executable(bluewake_native_math_guard_test
        ${BLUEWAKE_REPO_ROOT}/tests/native_math_guard_test.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/native_math.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/native_work_pool.c)
    target_include_directories(bluewake_native_math_guard_test PRIVATE ${BLUEWAKE_REPO_ROOT}/cmake/composite)
    target_link_libraries(bluewake_native_math_guard_test PRIVATE gxruntime)
    target_compile_options(bluewake_native_math_guard_test PRIVATE -ffp-contract=off)
    add_test(NAME bluewake_native_math_guard_test COMMAND bluewake_native_math_guard_test)

    target_compile_options(bluewake_native_math_guard_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_native_math_guard_test PRIVATE bluewake_posix_compat)
    set_target_properties(bluewake_native_math_guard_test PROPERTIES LINKER_LANGUAGE CXX)

    add_executable(bluewake_native_game_math_guard_test
        ${BLUEWAKE_REPO_ROOT}/tests/native_game_math_guard_test.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/native_game_math.c)
    target_include_directories(bluewake_native_game_math_guard_test PRIVATE ${BLUEWAKE_REPO_ROOT}/cmake/composite)
    target_link_libraries(bluewake_native_game_math_guard_test PRIVATE gxruntime)
    target_compile_options(bluewake_native_game_math_guard_test PRIVATE -ffp-contract=off)
    add_test(NAME bluewake_native_game_math_guard_test COMMAND bluewake_native_game_math_guard_test)

    target_compile_options(bluewake_native_game_math_guard_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_native_game_math_guard_test PRIVATE bluewake_posix_compat)
    set_target_properties(bluewake_native_game_math_guard_test PROPERTIES LINKER_LANGUAGE CXX)

    add_executable(bluewake_native_skin_guard_test
        ${BLUEWAKE_REPO_ROOT}/tests/native_skin_guard_test.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/native_skin.c)
    target_include_directories(bluewake_native_skin_guard_test PRIVATE ${BLUEWAKE_REPO_ROOT}/cmake/composite)
    target_link_libraries(bluewake_native_skin_guard_test PRIVATE gxruntime)
    target_compile_options(bluewake_native_skin_guard_test PRIVATE -ffp-contract=off)
    add_test(NAME bluewake_native_skin_guard_test COMMAND bluewake_native_skin_guard_test)

    target_compile_options(bluewake_native_skin_guard_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_native_skin_guard_test PRIVATE bluewake_posix_compat)
    set_target_properties(bluewake_native_skin_guard_test PROPERTIES LINKER_LANGUAGE CXX)

    add_executable(bluewake_native_vec_guard_test
        ${BLUEWAKE_REPO_ROOT}/tests/native_vec_guard_test.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/native_vec.c)
    target_include_directories(bluewake_native_vec_guard_test PRIVATE ${BLUEWAKE_REPO_ROOT}/cmake/composite)
    target_link_libraries(bluewake_native_vec_guard_test PRIVATE gxruntime)
    target_compile_options(bluewake_native_vec_guard_test PRIVATE -ffp-contract=off)
    add_test(NAME bluewake_native_vec_guard_test COMMAND bluewake_native_vec_guard_test)

    target_compile_options(bluewake_native_vec_guard_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_native_vec_guard_test PRIVATE bluewake_posix_compat)
    set_target_properties(bluewake_native_vec_guard_test PROPERTIES LINKER_LANGUAGE CXX)

    add_executable(bluewake_native_j3d_guard_test
        ${BLUEWAKE_REPO_ROOT}/tests/native_j3d_guard_test.c
        ${BLUEWAKE_REPO_ROOT}/cmake/composite/native_j3d.c)
    target_include_directories(bluewake_native_j3d_guard_test PRIVATE ${BLUEWAKE_REPO_ROOT}/cmake/composite)
    target_link_libraries(bluewake_native_j3d_guard_test PRIVATE gxruntime)
    target_compile_options(bluewake_native_j3d_guard_test PRIVATE -ffp-contract=off)
    add_test(NAME bluewake_native_j3d_guard_test COMMAND bluewake_native_j3d_guard_test)

    target_compile_options(bluewake_native_j3d_guard_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_native_j3d_guard_test PRIVATE bluewake_posix_compat)
    set_target_properties(bluewake_native_j3d_guard_test PROPERTIES LINKER_LANGUAGE CXX)

    add_executable(bluewake_inline_fp_test ${BLUEWAKE_REPO_ROOT}/tests/inline_fp_test.c)
    target_link_libraries(bluewake_inline_fp_test PRIVATE gxruntime)
    target_include_directories(bluewake_inline_fp_test PRIVATE ${BLUEWAKE_REPO_ROOT}/cmake/composite)
    target_compile_options(bluewake_inline_fp_test PRIVATE -ffp-contract=off)
    add_test(NAME bluewake_inline_fp_test COMMAND bluewake_inline_fp_test)
    target_compile_options(bluewake_inline_fp_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_inline_fp_test PRIVATE bluewake_posix_compat)
    set_target_properties(bluewake_inline_fp_test PROPERTIES LINKER_LANGUAGE CXX)

    add_executable(bluewake_module_cpu_contract_test ${BLUEWAKE_REPO_ROOT}/tests/module_cpu_contract_test.c)
    target_compile_options(bluewake_module_cpu_contract_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_module_cpu_contract_test PRIVATE gxruntime bluewake_posix_compat)
    set_target_properties(bluewake_module_cpu_contract_test PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(bluewake_module_cpu_contract_test PRIVATE
        ${GXRUNTIME_DIR}/include
        ${BLUEWAKE_RECOMPCORE}/Source/Core/Core/PowerPC/StaticRecomp)
    add_test(NAME bluewake_module_cpu_contract_test COMMAND bluewake_module_cpu_contract_test)

    foreach(_variant IN ITEMS plain global)
        add_executable(bluewake_guest_memory_${_variant}_test ${GXRUNTIME_DIR}/tests/guest_memory_bounds_test.c)
        target_include_directories(bluewake_guest_memory_${_variant}_test PRIVATE ${BLUEWAKE_COMPAT_INCLUDES})
        target_compile_options(bluewake_guest_memory_${_variant}_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
        target_link_libraries(bluewake_guest_memory_${_variant}_test PRIVATE gxruntime bluewake_posix_compat)
        set_target_properties(bluewake_guest_memory_${_variant}_test PROPERTIES LINKER_LANGUAGE CXX)
        if(_variant STREQUAL "global")
            target_compile_definitions(bluewake_guest_memory_${_variant}_test PRIVATE BW_GUEST_MEM1=bw_test_mem1 BW_GUEST_MEM1_SIZE=0x02000000u)
        endif()
        add_test(NAME bluewake_guest_memory_${_variant}_test COMMAND bluewake_guest_memory_${_variant}_test)
    endforeach()
    # Run by hand against archives of your own: bluewake_mod_archive_test FILE.zip FILE.rar FILE.7z
    add_executable(bluewake_mod_archive_test
        ${BLUEWAKE_REPO_ROOT}/tests/mod_archive_test.c ${BLUEWAKE_HOST_SRC}/mod_archive.c)
    target_include_directories(bluewake_mod_archive_test PRIVATE ${BLUEWAKE_HOST_SRC})
    target_link_libraries(bluewake_mod_archive_test PRIVATE bluewake_archive)
    if(WIN32)
        add_executable(bluewake_windows_disc_test ${BLUEWAKE_REPO_ROOT}/tests/windows_disc_test.c)
        target_include_directories(bluewake_windows_disc_test PRIVATE ${BLUEWAKE_HOST_SRC})
        target_compile_options(bluewake_windows_disc_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
        target_link_libraries(bluewake_windows_disc_test PRIVATE bluewake_posix_compat comctl32 comdlg32)
        target_compile_definitions(bluewake_windows_disc_test PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
        add_test(NAME bluewake_windows_disc_test COMMAND bluewake_windows_disc_test)
        set_tests_properties(bluewake_windows_disc_test PROPERTIES TIMEOUT 30)
    endif()

    enable_testing()
    add_executable(bluewake_card_dispatch_lock_test ${BLUEWAKE_REPO_ROOT}/tests/card_dispatch_lock_test.c)
    target_include_directories(bluewake_card_dispatch_lock_test PRIVATE ${BLUEWAKE_HOST_SRC} ${GXRUNTIME_DIR}/include ${BLUEWAKE_COMPAT_INCLUDES})
    target_compile_options(bluewake_card_dispatch_lock_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_card_dispatch_lock_test PRIVATE gxruntime bluewake_posix_compat)
    set_target_properties(bluewake_card_dispatch_lock_test PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME bluewake_card_dispatch_lock_test COMMAND bluewake_card_dispatch_lock_test)
    add_executable(bluewake_render_worker_identity_test ${BLUEWAKE_REPO_ROOT}/tests/render_worker_identity_test.cpp)
    target_include_directories(bluewake_render_worker_identity_test PRIVATE ${GXRUNTIME_DIR}/graphics/aurora/lib)
    target_link_libraries(bluewake_render_worker_identity_test PRIVATE gxruntime GXRuntime::aurora GXRuntime::gxcore)
    add_test(NAME bluewake_render_worker_identity_test COMMAND bluewake_render_worker_identity_test)
    set_tests_properties(bluewake_render_worker_identity_test PROPERTIES TIMEOUT 30)

    add_executable(bluewake_gxcore_test ${GXRUNTIME_DIR}/graphics/gxcore/tests/gxcore_wgsl_test.cpp)
    target_link_libraries(bluewake_gxcore_test PRIVATE GXRuntime::gxcore)
    add_test(NAME bluewake_gxcore_test COMMAND bluewake_gxcore_test)
    set_tests_properties(bluewake_gxcore_test PROPERTIES TIMEOUT 30)

    add_executable(bluewake_texture_encode_test ${GXRUNTIME_DIR}/graphics/gxcore/tests/gxcore_texture_encode_test.cpp)
    target_link_libraries(bluewake_texture_encode_test PRIVATE GXRuntime::gxcore)
    add_test(NAME bluewake_texture_encode_test COMMAND bluewake_texture_encode_test)

    add_executable(bluewake_runtime_test ${GXRUNTIME_DIR}/tests/runtime_tests.c)
    target_include_directories(bluewake_runtime_test PRIVATE ${BLUEWAKE_COMPAT_INCLUDES})
    target_compile_options(bluewake_runtime_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_runtime_test PRIVATE gxruntime bluewake_posix_compat)
    set_target_properties(bluewake_runtime_test PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME bluewake_runtime_test COMMAND bluewake_runtime_test)

    add_executable(bluewake_frontend_test ${GXRUNTIME_DIR}/graphics/frontend/tests/frontend_replay_test.cpp)
    target_link_libraries(bluewake_frontend_test PRIVATE GXRuntime::retail_gx_frontend)
    # Replay fixtures hold multiple complete frontend states (over 2 MiB).
    if(WIN32)
        target_link_options(bluewake_frontend_test PRIVATE "LINKER:/STACK:8388608")
    endif()
    add_test(NAME bluewake_frontend_test COMMAND bluewake_frontend_test)
    set_tests_properties(bluewake_frontend_test PROPERTIES TIMEOUT 30)

    include("${BLUEWAKE_REPO_ROOT}/cmake/fifo_trace_worker_test.cmake")

    add_executable(bluewake_renderer_shared_flags_test ${BLUEWAKE_REPO_ROOT}/tests/renderer_shared_flags_test.cpp)
    target_compile_definitions(bluewake_renderer_shared_flags_test PRIVATE GXRUNTIME_HAS_AURORA_RECOMP=1)
    target_include_directories(bluewake_renderer_shared_flags_test PRIVATE ${GXRUNTIME_DIR}/backends/aurora ${GXRUNTIME_DIR}/graphics/aurora/lib)
    target_link_libraries(bluewake_renderer_shared_flags_test PRIVATE gxruntime GXRuntime::aurora GXRuntime::gxcore)
    add_test(NAME bluewake_renderer_shared_flags_test COMMAND bluewake_renderer_shared_flags_test)
    set_tests_properties(bluewake_renderer_shared_flags_test PROPERTIES TIMEOUT 15)

add_executable(bluewake_gx_flush_metrics_test ${BLUEWAKE_REPO_ROOT}/tests/gx_flush_metrics_test.c)
target_include_directories(bluewake_gx_flush_metrics_test PRIVATE ${BLUEWAKE_HOST_SRC})
add_test(NAME bluewake_gx_flush_metrics_test COMMAND bluewake_gx_flush_metrics_test)


add_executable(bluewake_desktop_controller_test ${BLUEWAKE_REPO_ROOT}/tests/desktop_controller_test.cpp)
target_include_directories(bluewake_desktop_controller_test PRIVATE ${GXRUNTIME_DIR}/graphics/aurora/lib ${BLUEWAKE_HOST_SRC})
target_link_libraries(bluewake_desktop_controller_test PRIVATE gxruntime GXRuntime::aurora GXRuntime::gxcore)
add_test(NAME bluewake_desktop_controller_test COMMAND bluewake_desktop_controller_test)
set_tests_properties(bluewake_desktop_controller_test PROPERTIES TIMEOUT 15)


add_executable(bluewake_face_button_swap_test ${BLUEWAKE_REPO_ROOT}/tests/face_button_swap_test.c)
target_include_directories(bluewake_face_button_swap_test PRIVATE ${BLUEWAKE_HOST_SRC})
add_test(NAME bluewake_face_button_swap_test COMMAND bluewake_face_button_swap_test)


add_test(NAME bluewake_gc_adapter_proto_test COMMAND bluewake_gc_adapter_proto_test)
set_target_properties(bluewake_gc_adapter_proto_test PROPERTIES EXCLUDE_FROM_ALL OFF) # built with the other tests


add_executable(bluewake_audio_sink_test ${GXRUNTIME_DIR}/graphics/aurora/tests/audio_sink_test.cpp)
target_include_directories(bluewake_audio_sink_test PRIVATE ${GXRUNTIME_DIR}/graphics/aurora/lib)
target_link_libraries(bluewake_audio_sink_test PRIVATE gxruntime GXRuntime::aurora GXRuntime::gxcore)
add_test(NAME bluewake_audio_sink_test COMMAND bluewake_audio_sink_test)
set_tests_properties(bluewake_audio_sink_test PROPERTIES TIMEOUT 15)

    if(WIN32)
        add_executable(bluewake_windows_crash_driver src/win_crash.cpp ${BLUEWAKE_REPO_ROOT}/tests/windows_crash_driver.cpp)
        target_compile_definitions(bluewake_windows_crash_driver PRIVATE BLUEWAKE_ENABLE_DEVELOPER_TRACING=1)
        add_test(NAME bluewake_windows_crash_test COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File ${BLUEWAKE_REPO_ROOT}/tests/windows_crash_test.ps1 -Driver $<TARGET_FILE:bluewake_windows_crash_driver>)
        set_tests_properties(bluewake_windows_crash_test PROPERTIES TIMEOUT 30)
    endif()


add_executable(bluewake_crash_text_test ${BLUEWAKE_REPO_ROOT}/tests/crash_text_test.c)
add_test(NAME bluewake_crash_text_test COMMAND bluewake_crash_text_test)


add_executable(bluewake_pipeline_shutdown_test ${GXRUNTIME_DIR}/graphics/aurora/tests/pipeline_shutdown_test.cpp)
target_link_libraries(bluewake_pipeline_shutdown_test PRIVATE gxruntime GXRuntime::aurora GXRuntime::gxcore)
add_test(NAME bluewake_pipeline_shutdown_test COMMAND bluewake_pipeline_shutdown_test)
set_tests_properties(bluewake_pipeline_shutdown_test PROPERTIES TIMEOUT 45)


# The randomizer's host functions on a fake CPUState (the unit includes rando_seed.c and its own small site table) and
# the seed pack loader on a synthetic pack (BLUEWAKE_TEST_SEED_JSON=a real seed.json adds a check on it).
add_executable(bluewake_rando_seed_test ${BLUEWAKE_REPO_ROOT}/tests/rando_seed_test.c)
target_include_directories(bluewake_rando_seed_test PRIVATE ${BLUEWAKE_HOST_SRC} ${GXRUNTIME_DIR}/include
    ${BLUEWAKE_RECOMPCORE}/Source/Core/Core/PowerPC/StaticRecomp)
target_compile_definitions(bluewake_rando_seed_test PRIVATE
    "BLUEWAKE_RANDO_SITES_INC=\"${BLUEWAKE_REPO_ROOT}/tests/rando_seed_test_sites.inc\"")
target_compile_options(bluewake_rando_seed_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME bluewake_rando_seed_test COMMAND bluewake_rando_seed_test)
add_executable(bluewake_dvd_new_file_test ${BLUEWAKE_REPO_ROOT}/tests/dvd_new_file_test.c)
target_include_directories(bluewake_dvd_new_file_test PRIVATE ${GXRUNTIME_DIR}/include ${BLUEWAKE_COMPAT_INCLUDES})
target_compile_options(bluewake_dvd_new_file_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
target_link_libraries(bluewake_dvd_new_file_test PRIVATE gxruntime bluewake_posix_compat)
set_target_properties(bluewake_dvd_new_file_test PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME bluewake_dvd_new_file_test COMMAND bluewake_dvd_new_file_test)
add_executable(bluewake_rando_pack_test ${BLUEWAKE_REPO_ROOT}/tests/rando_pack_test.cpp src/rando_pack.cpp
    ${BLUEWAKE_HOST_SRC}/rando_seed.c)
target_include_directories(bluewake_rando_pack_test PRIVATE ${BLUEWAKE_HOST_SRC} src ${BLUEWAKE_RECOMPCORE}/Externals/picojson
    ${GXRUNTIME_DIR}/include ${BLUEWAKE_RECOMPCORE}/Source/Core/Core/PowerPC/StaticRecomp ${BLUEWAKE_COMPAT_INCLUDES})
target_compile_features(bluewake_rando_pack_test PRIVATE cxx_std_17)
add_test(NAME bluewake_rando_pack_test COMMAND bluewake_rando_pack_test)
add_executable(bluewake_rando_saves_test ${BLUEWAKE_REPO_ROOT}/tests/rando_saves_test.cpp src/rando_saves.cpp src/rando_pack.cpp
    ${BLUEWAKE_HOST_SRC}/rando_seed.c)
target_include_directories(bluewake_rando_saves_test PRIVATE ${BLUEWAKE_HOST_SRC} src ${BLUEWAKE_RECOMPCORE}/Externals/picojson
    ${GXRUNTIME_DIR}/include ${BLUEWAKE_RECOMPCORE}/Source/Core/Core/PowerPC/StaticRecomp ${BLUEWAKE_COMPAT_INCLUDES})
target_compile_features(bluewake_rando_saves_test PRIVATE cxx_std_17)
add_test(NAME bluewake_rando_saves_test COMMAND bluewake_rando_saves_test)

add_executable(bluewake_cvar_test ${BLUEWAKE_REPO_ROOT}/tests/cvar_test.cpp ${BLUEWAKE_HOST_SRC}/ship/cvar.cpp)
target_include_directories(bluewake_cvar_test PRIVATE ${BLUEWAKE_RECOMPCORE}/Externals/picojson)
target_compile_features(bluewake_cvar_test PRIVATE cxx_std_17)
add_test(NAME bluewake_cvar_test COMMAND bluewake_cvar_test)


add_executable(bluewake_launch_marker_test ${BLUEWAKE_REPO_ROOT}/tests/launch_marker_test.c)
target_include_directories(bluewake_launch_marker_test PRIVATE ${BLUEWAKE_HOST_SRC})
add_test(NAME bluewake_launch_marker_test COMMAND bluewake_launch_marker_test)
target_compile_options(bluewake_launch_marker_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
target_link_libraries(bluewake_launch_marker_test PRIVATE bluewake_posix_compat)

add_executable(bluewake_atomic_copy_race_test ${BLUEWAKE_REPO_ROOT}/tests/atomic_copy_race_test.c)
target_include_directories(bluewake_atomic_copy_race_test PRIVATE ${BLUEWAKE_HOST_SRC})
target_compile_options(bluewake_atomic_copy_race_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
target_link_libraries(bluewake_atomic_copy_race_test PRIVATE bluewake_posix_compat)
add_test(NAME bluewake_atomic_copy_race_test COMMAND bluewake_atomic_copy_race_test)

add_executable(bluewake_save_state_test ${BLUEWAKE_REPO_ROOT}/tests/save_state_test.c ${BLUEWAKE_HOST_SRC}/save_state.c)
target_include_directories(bluewake_save_state_test PRIVATE ${BLUEWAKE_HOST_SRC} ${BLUEWAKE_COMPAT_INCLUDES})
target_compile_options(bluewake_save_state_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
target_link_libraries(bluewake_save_state_test PRIVATE ZLIB::ZLIB bluewake_posix_compat)
add_test(NAME bluewake_save_state_test COMMAND bluewake_save_state_test)

include("${BLUEWAKE_REPO_ROOT}/cmake/host_state_save_test.cmake")
include("${BLUEWAKE_REPO_ROOT}/cmake/haptics_test.cmake")
target_include_directories(bluewake_host_state_save_failure_test PRIVATE ${BLUEWAKE_COMPAT_INCLUDES})
target_compile_options(bluewake_host_state_save_failure_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
target_link_libraries(bluewake_host_state_save_failure_test PRIVATE ZLIB::ZLIB bluewake_posix_compat)


add_executable(bluewake_restart_request_test ${BLUEWAKE_REPO_ROOT}/tests/restart_request_test.cpp)
add_test(NAME bluewake_restart_request_test COMMAND bluewake_restart_request_test)


add_executable(bluewake_memory_card_io_test ${BLUEWAKE_REPO_ROOT}/tests/memory_card_io_test.c)
target_include_directories(bluewake_memory_card_io_test PRIVATE ${GXRUNTIME_DIR}/include ${BLUEWAKE_COMPAT_INCLUDES})
target_compile_options(bluewake_memory_card_io_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME bluewake_memory_card_io_test COMMAND bluewake_memory_card_io_test)
set_tests_properties(bluewake_memory_card_io_test PROPERTIES TIMEOUT 15)
target_link_libraries(bluewake_memory_card_io_test PRIVATE bluewake_posix_compat)

    add_executable(bluewake_audio_dma_stereo_test ${BLUEWAKE_REPO_ROOT}/tests/audio_dma_stereo_test.c)
    target_include_directories(bluewake_audio_dma_stereo_test PRIVATE ${BLUEWAKE_HOST_SRC})
    target_compile_options(bluewake_audio_dma_stereo_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME bluewake_audio_dma_stereo_test COMMAND bluewake_audio_dma_stereo_test)
    add_executable(bluewake_bmg_patch_test ${BLUEWAKE_REPO_ROOT}/tests/bmg_patch_test.c)
    target_include_directories(bluewake_bmg_patch_test PRIVATE ${BLUEWAKE_HOST_SRC})
    target_compile_options(bluewake_bmg_patch_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME bluewake_bmg_patch_test COMMAND bluewake_bmg_patch_test)
    set_tests_properties(bluewake_bmg_patch_test PROPERTIES TIMEOUT 30)
    if(WIN32)
        add_executable(bluewake_windows_clock_test ${BLUEWAKE_REPO_ROOT}/tests/windows_clock_test.c)
        target_link_libraries(bluewake_windows_clock_test PRIVATE bluewake_posix_compat)
        target_compile_options(bluewake_windows_clock_test PRIVATE -Wall -Wextra -Werror)
        add_test(NAME bluewake_windows_clock_test COMMAND bluewake_windows_clock_test)
        set_tests_properties(bluewake_windows_clock_test PROPERTIES TIMEOUT 15)
    endif()
    add_executable(bluewake_feature_dispatch_test ${BLUEWAKE_REPO_ROOT}/tests/feature_dispatch_test.c)
    target_include_directories(bluewake_feature_dispatch_test PRIVATE
        ${BLUEWAKE_HOST_SRC} ${BLUEWAKE_COMPAT_INCLUDES})
    target_compile_options(bluewake_feature_dispatch_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_feature_dispatch_test PRIVATE gxruntime bluewake_posix_compat)
    set_target_properties(bluewake_feature_dispatch_test PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME bluewake_feature_dispatch_test COMMAND bluewake_feature_dispatch_test)
    set_tests_properties(bluewake_feature_dispatch_test PROPERTIES TIMEOUT 30)
    if(WIN32)
        add_executable(bluewake_windows_getenv_cache_test
            ${BLUEWAKE_REPO_ROOT}/tests/windows_getenv_cache_test.c)
        target_compile_options(bluewake_windows_getenv_cache_test PRIVATE -Wall -Wextra -Werror)
        add_test(NAME bluewake_windows_getenv_cache_test COMMAND bluewake_windows_getenv_cache_test)
        set_tests_properties(bluewake_windows_getenv_cache_test PROPERTIES TIMEOUT 15)
    endif()

    add_executable(bluewake_fps_watch_test
        ${BLUEWAKE_REPO_ROOT}/tests/fps_watch_test.c ${BLUEWAKE_HOST_SRC}/fps_watch.c)
    target_include_directories(bluewake_fps_watch_test PRIVATE
        ${BLUEWAKE_HOST_SRC} ${BLUEWAKE_COMPAT_INCLUDES})
    target_compile_options(bluewake_fps_watch_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_fps_watch_test PRIVATE gxruntime bluewake_posix_compat)
    set_target_properties(bluewake_fps_watch_test PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME bluewake_fps_watch_test COMMAND bluewake_fps_watch_test)
    set_tests_properties(bluewake_fps_watch_test PROPERTIES TIMEOUT 15)

    add_executable(bluewake_frame_interp_test
        ${GXRUNTIME_DIR}/graphics/aurora/tests/frame_interp_test.cpp)
    target_include_directories(bluewake_frame_interp_test PRIVATE ${BLUEWAKE_COMPAT_INCLUDES})
    target_compile_options(bluewake_frame_interp_test PRIVATE ${BLUEWAKE_COMPAT_FORCE_INCLUDE})
    target_link_libraries(bluewake_frame_interp_test PRIVATE gxruntime GXRuntime::aurora GXRuntime::gxcore bluewake_posix_compat)
    add_test(NAME bluewake_frame_interp_test COMMAND bluewake_frame_interp_test)
    set_tests_properties(bluewake_frame_interp_test PROPERTIES TIMEOUT 30)

    add_executable(bluewake_dsp_interpreter_layout_test
        ${BLUEWAKE_REPO_ROOT}/tests/dsp_interpreter_layout_test.cpp
        ${BLUEWAKE_REPO_ROOT}/tests/dsp_interpreter_layout_tables_first.cpp)
    target_include_directories(bluewake_dsp_interpreter_layout_test PRIVATE ${BLUEWAKE_REPO_ROOT}/tests)
    target_compile_options(bluewake_dsp_interpreter_layout_test PRIVATE -fno-ms-compatibility)
    target_link_libraries(bluewake_dsp_interpreter_layout_test PRIVATE bluewake_dsp_donor)
    add_test(NAME bluewake_dsp_interpreter_layout_test COMMAND bluewake_dsp_interpreter_layout_test)
    set_tests_properties(bluewake_dsp_interpreter_layout_test PROPERTIES TIMEOUT 15)
endif()

# The app folder: the executable, its runtime DLLs (SDL3, Dawn and the DXC
# shader compiler Dawn loads for Direct3D 12) and Aurora's bundled pipeline
# cache. The builder adds the game module and the prepared game files.
# The Windows seed (windows/resources) is the Mac's 213 pipelines and those a
# tour of the game asked for (towns, islands, dungeons and their bosses,
# interiors, the sea): 513, compiled on several threads at start, so a place's
# first visit draws everything from its first frame. Its rows are pipeline
# state keys, not game data.
if(WIN32)
    include(${GXRUNTIME_DIR}/graphics/aurora/cmake/AuroraCopyRuntimeDLLs.cmake)
    aurora_copy_runtime_dlls(BlueWake)
endif()
set(BLUEWAKE_PIPELINE_SEED "${BLUEWAKE_REPO_ROOT}/windows/resources/initial_pipeline_cache.db")
add_custom_command(TARGET BlueWake POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BLUEWAKE_PIPELINE_SEED}" $<TARGET_FILE_DIR:BlueWake>
    VERBATIM)
