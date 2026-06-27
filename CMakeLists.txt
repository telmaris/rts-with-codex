cmake_minimum_required(VERSION 3.16)
project(RtsGameTutorial)

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

find_package(raylib 3.0 QUIET)
find_package(Threads REQUIRED)

if (NOT raylib_FOUND)
    if (NOT raylib_INCLUDE_DIR OR NOT raylib_LIBRARY)
        message(FATAL_ERROR "raylib not found. Set raylib_INCLUDE_DIR and raylib_LIBRARY.")
    endif()

    add_library(raylib UNKNOWN IMPORTED)
    set_target_properties(raylib PROPERTIES
        IMPORTED_LOCATION "${raylib_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${raylib_INCLUDE_DIR}")
endif()
add_library(game "")

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
