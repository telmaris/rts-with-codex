cmake_minimum_required(VERSION 3.16)
project(RtsGameTutorial)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wno-narrowing)

find_package(raylib 3.0 REQUIRED)
find_package(Threads REQUIRED)
add_library(game "")

include_directories(inc)
add_subdirectory(src)

add_executable(rts src/main.cpp)
target_link_libraries(rts game raylib Threads::Threads dl m)