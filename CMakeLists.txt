cmake_minimum_required(VERSION 3.16)

# Version: single source of truth is the repository-root VERSION file.
# Reconfigure automatically whenever it changes (e.g. after a build-time patch bump).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RTS_VERSION_RAW LIMIT_COUNT 1)
string(STRIP "${RTS_VERSION_RAW}" RTS_VERSION)
if (NOT RTS_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION file malformed: '${RTS_VERSION}' (expected MAJOR.MINOR.PATCH)")
endif()

project(RtsGameTutorial VERSION ${RTS_VERSION} LANGUAGES CXX)

# Generate a header carrying the version into the binary (written to the build tree).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in"
    "${CMAKE_BINARY_DIR}/generated/Version.h"
    @ONLY)
include_directories("${CMAKE_BINARY_DIR}/generated")
message(STATUS "RTS version: ${RTS_VERSION}")

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_COVERAGE "Enable coverage reporting for unit tests" OFF)
option(BUILD_TESTING "Build unit tests" OFF)
include(CTest)

if (NOT MSVC)
    add_compile_options(-Wno-narrowing)
endif()

# When vcpkg provides statically-linked dependencies (x64-windows-static triplet),
# our own code must also use the static CRT to avoid MSVC "defaultlib conflicts".
if (MSVC AND DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(raylib 5.0 QUIET)
find_package(Threads REQUIRED)

if (NOT raylib_FOUND)
    if (raylib_INCLUDE_DIR AND raylib_LIBRARY)
        # Pre-built raylib supplied via -Draylib_INCLUDE_DIR / -Draylib_LIBRARY (local dev).
        add_library(raylib UNKNOWN IMPORTED)
        set_target_properties(raylib PROPERTIES
            IMPORTED_LOCATION "${raylib_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${raylib_INCLUDE_DIR}")
    else()
        # No pre-built supplied — fetch and build raylib 5.0 from source (CI / first-time setup).
        message(STATUS "raylib not found locally; fetching raylib 5.0 via FetchContent")
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG        5.0
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(raylib)
    endif()
endif()

# Force STATIC regardless of BUILD_SHARED_LIBS (vcpkg x64-windows sets it ON).
add_library(game STATIC "")

set(raygui_INCLUDE_DIR "" CACHE PATH "Path to directory containing raygui.h")
if (NOT raygui_INCLUDE_DIR AND EXISTS "${CMAKE_SOURCE_DIR}/deps/raygui/raygui.h")
    set(raygui_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/raygui")
endif()
if (raygui_INCLUDE_DIR)
    target_include_directories(game PUBLIC "${raygui_INCLUDE_DIR}")
endif()

include_directories(inc)
add_subdirectory(src)

if (ENABLE_COVERAGE)
    if (MSVC)
        target_compile_options(game PUBLIC /Od /Zi)
        target_link_options(game PUBLIC /DEBUG)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(game PUBLIC -O0 -g --coverage)
        target_link_options(game PUBLIC --coverage)
    else()
        message(WARNING "ENABLE_COVERAGE is supported only with MSVC or GCC/Clang gcov-compatible compilers.")
    endif()
endif()

add_executable(rts src/main.cpp)
target_link_libraries(game PUBLIC raylib)
target_link_libraries(rts game Threads::Threads)

if (WIN32)
    target_link_libraries(game PUBLIC ws2_32)
    target_link_libraries(rts winmm)
    if (MSVC)
        target_link_options(rts PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    endif()
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(rts dl m)
endif()

add_custom_command(TARGET rts POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:rts>/assets
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:rts>/saves
)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
