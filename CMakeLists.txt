# Standard routine
cmake_minimum_required(VERSION 3.16)
project(dirty_ram_mark
        VERSION 0.0.14 # CI_PROJECT_VERSION
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
        VERSION ${AUI_VERSION}
        CMAKE_ARGS -DAUI_COROUTINES=ON) # AUI disables coroutines on clang < 21 (Windows/macOS CI) by default


# Create the executable. This function automatically links all sources from the src/ folder,
# creates CMake target and places the resulting executable to bin/ folder.
aui_executable(${PROJECT_NAME})

# Link required libs
aui_link(${PROJECT_NAME} PRIVATE aui::core aui::views aui::json)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE dxgi)
endif ()

# Compile assets
aui_compile_assets(${PROJECT_NAME})

# Windows-only: kernel driver (physical memory access), built from driver/ and embedded as an asset (":driver/dirtyrammark.sys").
# Needs the WDK headers/libs (see driver/make.bat): found automatically (installed WDK or downloaded NuGet package),
# or pass -DWDK_ROOT=<dir with Include and Lib>.
if (WIN32)
    option(DIRTYRAMMARK_BUILD_DRIVER "Build and embed the kernel driver (requires WDK_ROOT)" ON)
    if (DIRTYRAMMARK_BUILD_DRIVER)
        # locate the WDK (a directory with Include/<ver>/km and Lib/<ver>/km): explicit WDK_ROOT, an installed WDK, or
        # the Microsoft.Windows.WDK.x64 NuGet package downloaded on demand.
        if (NOT WDK_ROOT AND DEFINED ENV{WDK_ROOT})
            set(WDK_ROOT "$ENV{WDK_ROOT}")
        endif ()
        if (NOT WDK_ROOT)
            foreach (_kits "$ENV{ProgramFiles\(x86\)}/Windows Kits/10" "$ENV{ProgramFiles}/Windows Kits/10")
                file(GLOB _km "${_kits}/Include/*/km/ntddk.h")
                if (_km)
                    set(WDK_ROOT "${_kits}")
                    break()
                endif ()
            endforeach ()
        endif ()
        if (NOT WDK_ROOT)
            set(_wdk_dir ${CMAKE_BINARY_DIR}/wdk)
            file(GLOB_RECURSE _km "${_wdk_dir}/ntddk.h")
            list(FILTER _km INCLUDE REGEX "/Include/[^/]+/km/ntddk\.h$")
            if (NOT _km)
                message(STATUS "WDK not found, downloading Microsoft.Windows.WDK.x64 from nuget.org (pass -DWDK_ROOT=... to avoid)")
                file(DOWNLOAD https://www.nuget.org/api/v2/package/Microsoft.Windows.WDK.x64 ${_wdk_dir}.nupkg
                        SHOW_PROGRESS STATUS _status)
                list(GET _status 0 _code)
                if (NOT _code EQUAL 0)
                    message(FATAL_ERROR "Unable to download the WDK: ${_status}. Set WDK_ROOT or -DDIRTYRAMMARK_BUILD_DRIVER=OFF.")
                endif ()
                file(ARCHIVE_EXTRACT INPUT ${_wdk_dir}.nupkg DESTINATION ${_wdk_dir})
                file(GLOB_RECURSE _km "${_wdk_dir}/ntddk.h")
                list(FILTER _km INCLUDE REGEX "/Include/[^/]+/km/ntddk\.h$")
            endif ()
            if (NOT _km)
                message(FATAL_ERROR "WDK headers not found in ${_wdk_dir}; set WDK_ROOT manually.")
            endif ()
            list(GET _km 0 _km)
            get_filename_component(WDK_ROOT "${_km}/../../../.." ABSOLUTE) # <root>/Include/<ver>/km/ntddk.h
        endif ()

        message(STATUS "WDK_ROOT: ${WDK_ROOT}")

        # cl.exe is only next to the compiler when it is MSVC; otherwise (clang) make.bat locates Visual Studio itself
        set(_cl_dir "")
        set(_cl_inc "")
        if (MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            get_filename_component(_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
            get_filename_component(_cl_inc "${_cl_dir}/../../../include" ABSOLUTE)
        endif ()
        set(DRIVER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/driver_out)
        set(DRIVER_SYS ${DRIVER_OUT_DIR}/dirtyrammark.sys)
        add_custom_command(
                OUTPUT ${DRIVER_SYS}
                COMMAND ${CMAKE_COMMAND} -E env "WDK_ROOT=${WDK_ROOT}" "CL_DIR=${_cl_dir}" "CL_INCLUDE=${_cl_inc}"
                        cmd /c "${CMAKE_CURRENT_SOURCE_DIR}/driver/make.bat" "${DRIVER_OUT_DIR}" "${DRIVER_OUT_DIR}/obj"
                DEPENDS driver/dirtyrammark.c driver/make.bat driver/sign.ps1
                COMMENT "Building dirtyrammark.sys"
                VERBATIM)
        add_custom_target(dirtyrammark_driver DEPENDS ${DRIVER_SYS})
        add_dependencies(${PROJECT_NAME} dirtyrammark_driver)

        # aui_compile_assets_add wants the file to exist at configure time; the real one replaces the placeholder at build time.
        if (NOT EXISTS ${DRIVER_SYS})
            file(MAKE_DIRECTORY ${DRIVER_OUT_DIR})
            file(WRITE ${DRIVER_SYS} "")
        endif ()
        aui_compile_assets_add(${PROJECT_NAME} ${DRIVER_SYS} driver/dirtyrammark.sys)
    endif ()
endif ()

# Enable tests
aui_enable_tests(${PROJECT_NAME})

# Setup icon, display name, etc
aui_app(TARGET ${PROJECT_NAME}
        NAME "DirtyRamMark"
        ID "ru.alex2772.dirtyrammark"
        VENDOR "AUI Project"
        ICON "assets/img/icon.svg"
)
