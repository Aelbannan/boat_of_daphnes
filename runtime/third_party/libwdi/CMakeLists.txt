# libwdi (LGPL-3.0-or-later): installs the WinUSB driver for the GameCube adapter, from Settings > Controls.
# See VENDORED.md. Built as a static library, wdi, from the sources in libwdi/ with a config of our own:
#   installer_x64.exe   the elevated helper libwdi runs (built here, then embedded)
#   embedder            turns the helper and the INF templates into embedded.h
#   wdi                 the library
# The WinUSB co-installers libwdi embeds come from the Windows Driver Kit's redistributables on the building
# machine (Windows Kits/10/Redist/wdf). Without them the library is not built and the button says so.
set(WDI_WDK "C:/Program Files (x86)/Windows Kits/10" CACHE PATH "Windows Kits folder with Redist/wdf")
if(NOT EXISTS "${WDI_WDK}/Redist/wdf/x64/WdfCoInstaller01011.dll" OR NOT EXISTS "${WDI_WDK}/Redist/wdf/x64/winusbcoinstaller2.dll")
    message(STATUS "libwdi: WDK redistributables not found in ${WDI_WDK}; the adapter driver installer is left out")
    return()
endif()
set(WDI_AVAILABLE TRUE PARENT_SCOPE)
set(WDI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/libwdi)
set(WDI_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config)
set(WDI_STAGE ${CMAKE_CURRENT_BINARY_DIR}/stage)
set(WDI_HELPER_DIR ${WDI_STAGE}/x64/Release/helper)
set(WDI_COMMON_DEFS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE WIN32_LEAN_AND_MEAN=0 _WIN64
    "WDK_DIR=\"${WDI_WDK}\"")
set(WDI_COMMON_FLAGS -w -fms-extensions)

add_executable(wdi_installer ${WDI_SRC}/installer.c)
set_target_properties(wdi_installer PROPERTIES OUTPUT_NAME installer_x64 RUNTIME_OUTPUT_DIRECTORY ${WDI_HELPER_DIR}
    LINKER_LANGUAGE C)
target_include_directories(wdi_installer PRIVATE ${WDI_CONFIG} ${WDI_SRC})
target_compile_definitions(wdi_installer PRIVATE ${WDI_COMMON_DEFS} ssize_t=intptr_t)
target_compile_options(wdi_installer PRIVATE ${WDI_COMMON_FLAGS})
target_link_libraries(wdi_installer PRIVATE setupapi newdev ole32 advapi32 shell32 user32)

add_executable(wdi_embedder ${WDI_SRC}/embedder.c)
target_include_directories(wdi_embedder PRIVATE ${WDI_CONFIG} ${WDI_SRC})
target_compile_definitions(wdi_embedder PRIVATE ${WDI_COMMON_DEFS} "SOLUTIONDIR=\"${WDI_STAGE}\"")
target_compile_options(wdi_embedder PRIVATE ${WDI_COMMON_FLAGS})
target_link_libraries(wdi_embedder PRIVATE shell32)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded.h
    COMMAND wdi_embedder ${CMAKE_CURRENT_BINARY_DIR}/embedded.h
    WORKING_DIRECTORY ${WDI_SRC}
    DEPENDS wdi_embedder wdi_installer ${WDI_SRC}/winusb.inf.in ${WDI_SRC}/libusb0.inf.in ${WDI_SRC}/libusbk.inf.in
            ${WDI_SRC}/usbser.inf.in
    COMMENT "libwdi: embedding the installer helper and INF templates")

add_library(wdi STATIC
    ${WDI_SRC}/logging.c ${WDI_SRC}/tokenizer.c ${WDI_SRC}/vid_data.c ${WDI_SRC}/pki.c ${WDI_SRC}/libwdi_dlg.c
    ${WDI_SRC}/libwdi.c ${CMAKE_CURRENT_BINARY_DIR}/embedded.h)
set_target_properties(wdi PROPERTIES LINKER_LANGUAGE C)
target_include_directories(wdi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${WDI_SRC} ${WDI_CONFIG} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(wdi PRIVATE ${WDI_COMMON_DEFS})
target_compile_options(wdi PRIVATE ${WDI_COMMON_FLAGS})
target_link_libraries(wdi PUBLIC setupapi ole32 ntdll crypt32 advapi32 shell32 user32 gdi32 comctl32 wintrust version)
