cmake_minimum_required(VERSION 3.16)

# Standalone dev tool: texture viewer/editor for everything under assets/textures.
#
# Same deal as tools/tech-tree-editor: deliberately NOT wired into the game's
# top-level CMakeLists, so a broken tool can never break the game build. It does
# compile a handful of the game's REAL translation units by relative path
# (RTS_SHARED_SOURCES below) — the .rtsdata tokenizer and the shared UI widgets —
# because the tool reads files the game must read the same way.
#
# Pilot stage: read-only viewer. Nothing here writes to assets/.

project(TextureEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(RTS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if (NOT EXISTS "${RTS_ROOT}/inc/ui/Renderer.h")
    message(FATAL_ERROR
        "Game sources not found at ${RTS_ROOT}. This tool shares the game's data "
        "parser and only builds from inside a full rts-with-codex checkout.")
endif()
message(STATUS "Game root: ${RTS_ROOT}")

find_package(raylib 5.0 QUIET)
if (NOT raylib_FOUND)
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
# a .rtsdata file is tokenized and how the shared UI chrome renders.
set(RTS_SHARED_SOURCES
    # The textures.rtsdata model + parser + serializer, shared with the game's
    # building-art runtime so editor saves are visible in the next game launch.
    "${RTS_ROOT}/src/data/TextureConfig.cpp"
    "${RTS_ROOT}/src/data/RtsDataFile.cpp"
    "${RTS_ROOT}/src/data/Resource.cpp"
    # ResourcePool's constructor calls FindEquipmentProfile.
    "${RTS_ROOT}/src/data/Equipment.cpp"
    # Shared UI: text metrics and input widgets, already extracted out of Gui.cpp
    # for tools/tech-tree-editor.
    "${RTS_ROOT}/src/ui/UiText.cpp"
    "${RTS_ROOT}/src/ui/UiWidgets.cpp")

if (WIN32)
    # Same reason as the game: raylib references timeBeginPeriod/timeEndPeriod but
    # linking winmm.lib would collide with raylib's own PlaySound symbol.
    list(APPEND RTS_SHARED_SOURCES "${RTS_ROOT}/src/WinmmStubs.cpp")
endif()

add_executable(texture_editor
    src/main.cpp
    src/TextureCatalog.cpp
    src/GameBindings.cpp
    src/Document.cpp
    src/EditorWidgets.cpp
    src/AtlasGrid.cpp
    src/TabCommon.cpp
    src/TabTerrain.cpp
    src/TabBuildings.cpp
    src/TabResources.cpp
    src/TabAssets.cpp
    ${RTS_SHARED_SOURCES})

source_group("shared (game)" FILES ${RTS_SHARED_SOURCES})

target_include_directories(texture_editor PRIVATE "${RTS_ROOT}/inc" src)
target_link_libraries(texture_editor PRIVATE raylib)

# Absolute paths baked in so the tool can be launched from any working directory
# (unlike the game, which resolves "assets/..." relative to the exe).
target_compile_definitions(texture_editor PRIVATE
    RTS_ASSETS_DIR="${RTS_ROOT}/assets")

if (UNIX AND NOT APPLE)
    target_link_libraries(texture_editor PRIVATE dl m pthread)
endif()
