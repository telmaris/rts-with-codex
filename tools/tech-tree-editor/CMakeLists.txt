cmake_minimum_required(VERSION 3.16)

# Standalone dev tool: research-tree viewer/editor for assets/data/*.rtsdata.
#
# The editor links the same production parser and shared UI targets as the game.
# This keeps editor round-trips and runtime loading on one implementation.

project(TechTreeEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(RTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if (NOT EXISTS "${RTS_ROOT}/inc/research/Technology.h")
    message(FATAL_ERROR
        "Game sources not found at ${RTS_ROOT}. This tool shares the game's parser "
        "and only builds from inside a full rts-with-codex checkout.")
endif()
message(STATUS "Game root: ${RTS_ROOT}")

find_package(raylib 5.0 QUIET)
if (NOT TARGET raylib AND NOT raylib_FOUND)
    if (raylib_INCLUDE_DIR AND raylib_LIBRARY)
        add_library(raylib UNKNOWN IMPORTED)
        set_target_properties(raylib PROPERTIES
            IMPORTED_LOCATION "${raylib_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${raylib_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR
            "raylib not found. Pass -Draylib_INCLUDE_DIR=... -Draylib_LIBRARY=... "
            "(build_and_run.ps1 does this for you).")
    endif()
endif()

# Game translation units compiled verbatim — the single source of truth for how
# a .rtsdata research file is tokenized, parsed and what a modifier means.
set(RTS_SHARED_SOURCES
    "${RTS_ROOT}/src/research/Technology.cpp"
    "${RTS_ROOT}/src/core/Log.cpp")

if (WIN32)
    # Same reason as the game: raylib references timeBeginPeriod/timeEndPeriod but
    # linking winmm.lib would collide with raylib's own PlaySound symbol.
    list(APPEND RTS_SHARED_SOURCES "${RTS_ROOT}/src/WinmmStubs.cpp")
endif()

add_executable(tech_tree_editor
    src/main.cpp
    src/TreeModel.cpp
    src/TreeView.cpp
    src/TreeSerializer.cpp
    src/BonusCalculator.cpp
    src/Inspector.cpp
    ${RTS_SHARED_SOURCES})

source_group("shared (game)" FILES ${RTS_SHARED_SOURCES})

target_include_directories(tech_tree_editor PRIVATE "${RTS_ROOT}/inc" src)
target_link_libraries(tech_tree_editor PRIVATE raylib rts_data rts_ui_common)

# Absolute paths baked in so the tool can be launched from any working directory
# (unlike the game, which resolves "assets/..." relative to the exe).
target_compile_definitions(tech_tree_editor PRIVATE
    RTS_ASSETS_DIR="${RTS_ROOT}/assets")

if (UNIX AND NOT APPLE)
    target_link_libraries(tech_tree_editor PRIVATE dl m pthread)
endif()

if (BUILD_TESTING)
    add_test(NAME tech_tree_editor_selftest
             COMMAND tech_tree_editor --selftest "${CMAKE_CURRENT_BINARY_DIR}/selftest")
    set_tests_properties(tech_tree_editor_selftest PROPERTIES LABELS "data;fast")
endif()
