# Standard routine
cmake_minimum_required(VERSION 3.16)
project(dirty_ram_mark
        VERSION 0.0.13 # CI_PROJECT_VERSION
    )

option(BUILD_SHARED_LIBS "Build shared libs" OFF)
option(AUIB_FORCE_PRECOMPILED "AUI.Boot: force precompiled dependencies" OFF)
option(AUIB_NO_PRECOMPILED "AUI.Boot: disable precompiled dependencies" OFF)

set(AUI_VERSION v8.0.0-rc.32)

# Use AUI.Boot
include(aui.boot.cmake)

# import AUI
auib_import(aui https://github.com/aui-framework/aui
        COMPONENTS core views curl json crypt
        VERSION ${AUI_VERSION})


# Create the executable. This function automatically links all sources from the src/ folder,
# creates CMake target and places the resulting executable to bin/ folder.
aui_executable(${PROJECT_NAME})

# Link required libs
aui_link(${PROJECT_NAME} PRIVATE aui::core aui::views aui::json)

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
