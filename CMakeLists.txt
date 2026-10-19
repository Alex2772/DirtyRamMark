# Standard routine
cmake_minimum_required(VERSION 3.16)
project(dirty_ram_mark
        VERSION 0.0.15 # CI_PROJECT_VERSION
    )

option(BUILD_SHARED_LIBS "Build shared libs" OFF)
option(AUIB_FORCE_PRECOMPILED "AUI.Boot: force precompiled dependencies" OFF)
option(AUIB_NO_PRECOMPILED "AUI.Boot: disable precompiled dependencies" OFF)

set(AUI_VERSION 8723aa9ebcc3912344c2bc87fc645173639c1152)

# Use AUI.Boot
include(aui.boot.cmake)

# import AUI
auib_import(aui https://github.com/aui-framework/aui
        COMPONENTS core views curl json crypt remote_tools
        VERSION ${AUI_VERSION}
        CMAKE_ARGS -DAUI_COROUTINES=ON) # AUI disables coroutines on clang < 21 (Windows/macOS CI) by default


# Create the executable. This function automatically links all sources from the src/ folder,
# creates CMake target and places the resulting executable to bin/ folder.
aui_executable(${PROJECT_NAME})

# Link required libs
aui_link(${PROJECT_NAME} PRIVATE aui::core aui::views aui::json)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE dxgi dwmapi)
endif ()

# CPU stress test: let the compiler fuse multiply-add into FMA where the target has it
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -ffp-contract=fast)
elseif (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /fp:contract)
endif ()

# Linux-only: background blur on Wayland (ext-background-effect-v1 / org_kde_kwin_blur). Optional: needs libwayland-client
# headers and wayland-scanner; without them the window simply stays opaque.
if (UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
        find_program(WAYLAND_SCANNER wayland-scanner)
    endif ()
    if (WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER)
        set(WAYLAND_PROTOCOLS_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/wayland_protocols)
        file(MAKE_DIRECTORY ${WAYLAND_PROTOCOLS_GEN_DIR})
        foreach (_protocol ext-background-effect-v1 kde-blur)
            set(_xml ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux/protocols/${_protocol}.xml)
            set(_header ${WAYLAND_PROTOCOLS_GEN_DIR}/${_protocol}-client-protocol.h)
            set(_code ${WAYLAND_PROTOCOLS_GEN_DIR}/${_protocol}-protocol.c)
            add_custom_command(
                    OUTPUT ${_header} ${_code}
                    COMMAND ${WAYLAND_SCANNER} client-header ${_xml} ${_header}
                    COMMAND ${WAYLAND_SCANNER} private-code ${_xml} ${_code}
                    DEPENDS ${_xml}
                    VERBATIM)
            target_sources(${PROJECT_NAME} PRIVATE ${_header} ${_code})
        endforeach ()
        target_include_directories(${PROJECT_NAME} PRIVATE ${WAYLAND_PROTOCOLS_GEN_DIR})
        target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_definitions(${PROJECT_NAME} PRIVATE DIRTYRAMMARK_WAYLAND_BLUR=1)
    else ()
        message(STATUS "wayland-client/wayland-scanner not found: background blur is disabled")
    endif ()
endif ()

# Compile assets
aui_compile_assets(${PROJECT_NAME})

# Enable tests
aui_enable_tests(${PROJECT_NAME})

# Setup icon, display name, etc
aui_app(TARGET ${PROJECT_NAME}
        NAME "DirtyRamMark"
        ID "ru.alex2772.dirtyrammark"
        VENDOR "AUI Project"
        ICON "assets/img/icon.svg"
)

# hotswap (hot code reload): debug builds on Linux only
if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND AUI_PLATFORM_LINUX)
    aui_link(${PROJECT_NAME} PRIVATE aui::remote_tools)
    aui_enable_hotswap(${PROJECT_NAME})
endif()
