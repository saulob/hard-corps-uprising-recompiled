# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(hard_corps_uprising VERSION 0.6.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1 and tools/package-linux.sh unpack the pinned SDK.
if(WIN32)
    set(HCU_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")
else()
    set(HCU_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/linux-amd64")
endif()
list(APPEND CMAKE_PREFIX_PATH "${HCU_SDK_DIR}")

# Release binaries must not carry the build machine's folders (__FILE__ in log calls).
add_compile_options("$<$<CONFIG:Release>:-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>")

include(generated/rexglue.cmake)

# TRG Launcher: the third_party/trg-launcher submodule, or a local checkout
# named by HCU_TRG_LAUNCHER_DIR while the submodule is not checked out.
set(HCU_TRG_LAUNCHER_DIR "$ENV{HCU_TRG_LAUNCHER_DIR}" CACHE PATH
    "Local TRG Launcher checkout, used when third_party/trg-launcher is empty")
set(_trg_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/trg-launcher")
if(NOT EXISTS "${_trg_dir}/CMakeLists.txt")
    set(_trg_dir "${HCU_TRG_LAUNCHER_DIR}")
endif()
if(NOT EXISTS "${_trg_dir}/CMakeLists.txt")
    message(FATAL_ERROR "TRG Launcher not found: run `git submodule update --init third_party/trg-launcher`")
endif()

# The launcher draws inside the runtime's own Dear ImGui frame, so it must use
# the ImGui compiled into rexruntime (one context), never a downloaded copy.
add_library(hcu_imgui INTERFACE)
target_link_libraries(hcu_imgui INTERFACE rex::runtime)
set(TRG_LAUNCHER_IMGUI_TARGET hcu_imgui CACHE STRING "" FORCE)
set(TRG_LAUNCHER_STANDALONE OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_FETCH_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_trg_dir}" trg-launcher)
# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_launcher PROPERTIES CXX_STANDARD 17)
# TRG defines this only for cl.exe; clang++ needs it too for the MSVC CRT.
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
get_directory_property(HCU_TRG_VERSION DIRECTORY "${_trg_dir}" DEFINITION PROJECT_VERSION)

set(HCU_SOURCES
    src/main.cpp
    src/achievement_alias.cpp
    src/achievements_overlay.cpp
    src/aspect_render.cpp
    src/cheat_menu.cpp
    src/cheats.cpp
    src/crt_native.cpp
    src/frame_counter.cpp
    src/game_files.cpp
    src/gpu_fence_wait.cpp
    src/graphics_adapters.cpp
    src/launcher.cpp
    src/launcher_achievements.cpp
    src/legacy_settings.cpp
    src/live_fix.cpp
    src/live_stats.cpp
    src/local_stats.cpp
    src/modal_dialog.cpp
    src/notice_dialog.cpp
    src/platform.cpp
    src/settings.cpp
    src/skip_intro.cpp
    src/stfs.cpp
    src/user_data.cpp
    src/window_host.cpp
)
if(WIN32)
    # Resource 1 is the first icon in the executable: Explorer, and SDL's window class, use it.
    enable_language(RC)
    set(HCU_APP_RC "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/hard_corps_uprising.rc")
    set_source_files_properties("${HCU_APP_RC}" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/app-icon.ico")
    list(APPEND HCU_SOURCES "${HCU_APP_RC}")
    add_executable(hard_corps_uprising WIN32 ${HCU_SOURCES})
else()
    add_executable(hard_corps_uprising ${HCU_SOURCES})
endif()

rexglue_setup_target(hard_corps_uprising GPU_PLUGINS xenos)
target_link_libraries(hard_corps_uprising PRIVATE trg::launcher)
if(WIN32)
    target_link_libraries(hard_corps_uprising PRIVATE shcore synchronization dxgi)
    # The SDK stages its DLLs only when the program links, so a rebuilt runtime or GPU plugin has to
    # make it link again; tools/build.ps1 also checks the staged copies.
    foreach(_sdk_dll rexruntime rexruntimed rexruntimerd rexgpu-xenos rexgpu-xenosd rexgpu-xenosrd)
        if(EXISTS "${HCU_SDK_DIR}/bin/${_sdk_dll}.dll")
            set_property(TARGET hard_corps_uprising APPEND PROPERTY LINK_DEPENDS
                "${HCU_SDK_DIR}/bin/${_sdk_dll}.dll")
        endif()
    endforeach()
else()
    # The SDK stages the runtime beside the program only on Windows and macOS; the RPATH is $ORIGIN.
    add_custom_command(TARGET hard_corps_uprising POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rex::runtime>"
            "$<TARGET_FILE_DIR:hard_corps_uprising>"
        VERBATIM
    )
    # The post-build copies (this one and the SDK's GPU plugin copy) only run when the program links, so a
    # rebuilt runtime or GPU plugin has to make it link again; tools/package-linux.sh also checks the copies.
    foreach(_sdk_lib rexruntime rexruntimed rexruntimerd rexgpu-xenos rexgpu-xenosd rexgpu-xenosrd)
        if(EXISTS "${HCU_SDK_DIR}/lib/lib${_sdk_lib}.so")
            set_property(TARGET hard_corps_uprising APPEND PROPERTY LINK_DEPENDS
                "${HCU_SDK_DIR}/lib/lib${_sdk_lib}.so")
        endif()
    endforeach()
endif()
# Every build checks that tools/rexglue holds the runtime and GPU plugin built from this branch's patches, and that
# the copies next to the program are those; tools/build.ps1 and tools/build.sh rebuild them when they are not.
if(WIN32)
    # Windows PowerShell has a fixed path, unlike a Store pwsh; the module path a parent pwsh sets breaks it.
    find_program(HCU_WINDOWS_POWERSHELL NAMES powershell REQUIRED)
    set(HCU_REXGLUE_CHECK "${CMAKE_COMMAND}" -E env --unset=PSModulePath
        "${HCU_WINDOWS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.ps1" -Check -Config $<CONFIG>)
    set(HCU_REXGLUE_STAGED -Staged)
else()
    find_program(HCU_BASH bash REQUIRED)
    set(HCU_REXGLUE_CHECK "${HCU_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-patched-rexruntime.sh"
        --check "${HCU_SDK_DIR}" --config $<CONFIG>)
    set(HCU_REXGLUE_STAGED --staged)
endif()
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo|Release)$")
    message(FATAL_ERROR "Set CMAKE_BUILD_TYPE to Debug, RelWithDebInfo or Release (the presets and tools/build.ps1 "
        "do): the ReXGlue runtime and GPU plugin are checked per configuration")
endif()
add_custom_target(hcu_rexglue_check COMMAND ${HCU_REXGLUE_CHECK} VERBATIM)
add_dependencies(hard_corps_uprising hcu_rexglue_check)
add_custom_target(hcu_rexglue_staging ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rex::runtime>" "$<TARGET_FILE:rex::gpu-xenos>"
        "$<TARGET_FILE_DIR:hard_corps_uprising>"
    COMMAND ${HCU_REXGLUE_CHECK} ${HCU_REXGLUE_STAGED} "$<TARGET_FILE_DIR:hard_corps_uprising>"
    VERBATIM
)
add_dependencies(hcu_rexglue_staging hard_corps_uprising)
# ThinLTO in Release and RelWithDebInfo (the default of tools/build.ps1); the runtime and GPU plugin are
# prebuilt. Debug compiles at -O0, which marks every function optnone, so LTO would optimize nothing there.
function(hcu_enable_thinlto target)
    set(_lto "$<CONFIG:Release,RelWithDebInfo>")
    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        # lld-link runs the LTO backend at -O2 unless told otherwise; Release compiles at -O3.
        set(_link -flto=thin "$<$<CONFIG:Release>:-Wl,/opt:lldlto=3>"
            "-Wl,/lldltocache:${CMAKE_BINARY_DIR}/lto-cache")
    elseif(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_link -flto=thin -fuse-ld=lld "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto-cache")
    else()
        return()
    endif()
    target_compile_options(${target} PRIVATE "$<$<AND:${_lto},$<COMPILE_LANGUAGE:CXX>>:-flto=thin>")
    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "OBJECT_LIBRARY")
        target_link_options(${target} PRIVATE "$<${_lto}:${_link}>")
    endif()
endfunction()
hcu_enable_thinlto(hard_corps_uprising)
if(TARGET hard_corps_uprising_recomp)
    hcu_enable_thinlto(hard_corps_uprising_recomp)
endif()
target_compile_definitions(hard_corps_uprising PRIVATE
    HCU_PORT_VERSION="${PROJECT_VERSION}"
    HCU_TRG_VERSION="${HCU_TRG_VERSION}")

# The runtime reads its cvars from <exe dir>/hard_corps_uprising.toml.
add_custom_command(TARGET hard_corps_uprising POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/hard_corps_uprising.toml"
        "$<TARGET_FILE_DIR:hard_corps_uprising>"
    VERBATIM
)

# Installer tests, not part of the default build: cmake --build <dir> --target hcu_installer_tests
add_executable(hcu_installer_tests EXCLUDE_FROM_ALL
    tests/installer_tests.cpp
    src/game_files.cpp
    src/stfs.cpp
)
target_include_directories(hcu_installer_tests PRIVATE src)

# Cheat guard tests, not part of the default build: cmake --build <dir> --target hcu_cheats_tests
add_executable(hcu_cheats_tests EXCLUDE_FROM_ALL tests/cheats_tests.cpp)
target_include_directories(hcu_cheats_tests PRIVATE src)
target_compile_definitions(hcu_cheats_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Frame counter tests, not part of the default build: cmake --build <dir> --target hcu_frame_counter_tests
add_executable(hcu_frame_counter_tests EXCLUDE_FROM_ALL tests/frame_counter_tests.cpp)
target_include_directories(hcu_frame_counter_tests PRIVATE src)
target_compile_definitions(hcu_frame_counter_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Skip intro setting tests, not part of the default build: cmake --build <dir> --target hcu_skip_intro_tests
add_executable(hcu_skip_intro_tests EXCLUDE_FROM_ALL tests/skip_intro_tests.cpp)
target_include_directories(hcu_skip_intro_tests PRIVATE src)
target_compile_definitions(hcu_skip_intro_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Multisampling setting tests, not part of the default build: cmake --build <dir> --target hcu_multisampling_tests
add_executable(hcu_multisampling_tests EXCLUDE_FROM_ALL tests/multisampling_tests.cpp)
target_include_directories(hcu_multisampling_tests PRIVATE src)
target_compile_definitions(hcu_multisampling_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Aspect Ratio tests, not part of the default build: cmake --build <dir> --target hcu_aspect_ratio_tests
add_executable(hcu_aspect_ratio_tests EXCLUDE_FROM_ALL tests/aspect_ratio_tests.cpp)
target_include_directories(hcu_aspect_ratio_tests PRIVATE src)
target_compile_definitions(hcu_aspect_ratio_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Internal render resolution tests, not part of the default build:
# cmake --build <dir> --target hcu_internal_render_scale_tests
add_executable(hcu_internal_render_scale_tests EXCLUDE_FROM_ALL tests/internal_render_scale_tests.cpp)
target_include_directories(hcu_internal_render_scale_tests PRIVATE src)
target_link_libraries(hcu_internal_render_scale_tests PRIVATE trg::launcher)
target_compile_definitions(hcu_internal_render_scale_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Graphics card setting tests, not part of the default build: cmake --build <dir> --target hcu_graphics_adapter_tests
add_executable(hcu_graphics_adapter_tests EXCLUDE_FROM_ALL tests/graphics_adapter_tests.cpp src/graphics_adapters.cpp)
target_include_directories(hcu_graphics_adapter_tests PRIVATE src)
target_link_libraries(hcu_graphics_adapter_tests PRIVATE trg::launcher)
if(WIN32)
    target_link_libraries(hcu_graphics_adapter_tests PRIVATE dxgi d3d12)
endif()
target_compile_definitions(hcu_graphics_adapter_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Relaunch tests, not part of the default build: cmake --build <dir> --target hcu_relaunch_tests
add_executable(hcu_relaunch_tests EXCLUDE_FROM_ALL tests/relaunch_tests.cpp)
target_include_directories(hcu_relaunch_tests PRIVATE src)
target_compile_definitions(hcu_relaunch_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Cursor auto-hide and window title tests, not part of the default build:
# cmake --build <dir> --target hcu_window_host_tests
add_executable(hcu_window_host_tests EXCLUDE_FROM_ALL tests/window_host_tests.cpp)
target_include_directories(hcu_window_host_tests PRIVATE src)
target_compile_definitions(hcu_window_host_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Launcher window placement tests, not part of the default build:
# cmake --build <dir> --target hcu_window_placement_tests
add_executable(hcu_window_placement_tests EXCLUDE_FROM_ALL tests/window_placement_tests.cpp)
target_include_directories(hcu_window_placement_tests PRIVATE src)

# Offline Live tests (stats store, leaderboard answers, guest buffers, overlay rules), not part of the default
# build: cmake --build <dir> --target hcu_live_tests
add_executable(hcu_live_tests EXCLUDE_FROM_ALL
    tests/live_tests.cpp
    src/live_stats.cpp
    src/local_stats.cpp
)
target_include_directories(hcu_live_tests PRIVATE src)
target_include_directories(hcu_live_tests SYSTEM PRIVATE "${HCU_SDK_DIR}/include")
target_compile_definitions(hcu_live_tests PRIVATE HCU_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# User data tests (portable root and migration), not part of the default build:
# cmake --build <dir> --target hcu_user_data_tests
add_executable(hcu_user_data_tests EXCLUDE_FROM_ALL
    tests/user_data_tests.cpp
    src/user_data.cpp
    src/local_stats.cpp
)
target_include_directories(hcu_user_data_tests PRIVATE src)
target_include_directories(hcu_user_data_tests SYSTEM PRIVATE "${HCU_SDK_DIR}/include" "${_trg_dir}/include")

# The tests that compile game code are built like the game.
foreach(_test installer cheats frame_counter skip_intro multisampling internal_render_scale graphics_adapter window_host window_placement live user_data)
    hcu_enable_thinlto(hcu_${_test}_tests)
endforeach()

# Allocator stress test, not part of the default build: cmake --build <dir> --target hcu_physical_heap_stress
add_executable(hcu_physical_heap_stress EXCLUDE_FROM_ALL tests/physical_heap_stress.cpp)
target_link_libraries(hcu_physical_heap_stress PRIVATE rex::runtime)
add_custom_command(TARGET hcu_physical_heap_stress POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rex::runtime>"
        "$<TARGET_FILE_DIR:hcu_physical_heap_stress>"
    VERBATIM
)
if(NOT WIN32)
    set_target_properties(hcu_physical_heap_stress PROPERTIES BUILD_RPATH "$ORIGIN")
endif()

# Exit Game shutdown order test, not part of the default build: cmake --build <dir> --target hcu_shutdown_order_tests
add_executable(hcu_shutdown_order_tests EXCLUDE_FROM_ALL tests/shutdown_order_tests.cpp)
target_link_libraries(hcu_shutdown_order_tests PRIVATE rex::runtime)
add_custom_command(TARGET hcu_shutdown_order_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rex::runtime>"
        "$<TARGET_FILE_DIR:hcu_shutdown_order_tests>"
    VERBATIM
)
if(NOT WIN32)
    set_target_properties(hcu_shutdown_order_tests PROPERTIES BUILD_RPATH "$ORIGIN")
endif()

# Guest thread shutdown test, not part of the default build: cmake --build <dir> --target hcu_title_thread_shutdown_tests
add_executable(hcu_title_thread_shutdown_tests EXCLUDE_FROM_ALL tests/title_thread_shutdown_tests.cpp)
target_link_libraries(hcu_title_thread_shutdown_tests PRIVATE rex::runtime)
add_custom_command(TARGET hcu_title_thread_shutdown_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rex::runtime>"
        "$<TARGET_FILE_DIR:hcu_title_thread_shutdown_tests>"
    VERBATIM
)
if(NOT WIN32)
    set_target_properties(hcu_title_thread_shutdown_tests PROPERTIES BUILD_RPATH "$ORIGIN")
endif()

# RenderDoc detection test (Linux), not part of the default build: cmake --build <dir> --target hcu_renderdoc_detect_tests
if(UNIX AND NOT APPLE)
    # The fake librenderdoc.so goes into renderdoc/, out of reach of the runtime's $ORIGIN RUNPATH.
    add_library(hcu_fake_renderdoc SHARED EXCLUDE_FROM_ALL tests/fake_renderdoc.cpp)
    set_target_properties(hcu_fake_renderdoc PROPERTIES OUTPUT_NAME renderdoc
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/renderdoc")
    add_executable(hcu_renderdoc_detect_tests EXCLUDE_FROM_ALL tests/renderdoc_detect_tests.cpp)
    target_link_libraries(hcu_renderdoc_detect_tests PRIVATE rex::runtime rex::renderdoc ${CMAKE_DL_LIBS})
    target_compile_options(hcu_renderdoc_detect_tests PRIVATE -Wall -Wextra)
    add_dependencies(hcu_renderdoc_detect_tests hcu_fake_renderdoc)
    add_custom_command(TARGET hcu_renderdoc_detect_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rex::runtime>"
            "$<TARGET_FILE_DIR:hcu_renderdoc_detect_tests>"
        VERBATIM
    )
    set_target_properties(hcu_renderdoc_detect_tests PROPERTIES BUILD_RPATH "$ORIGIN")
endif()
