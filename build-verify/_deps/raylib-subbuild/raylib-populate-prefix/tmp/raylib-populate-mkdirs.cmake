# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "F:/Projekty/gamedev/rts-with-codex/build-verify/_deps/raylib-src"
  "F:/Projekty/gamedev/rts-with-codex/build-verify/_deps/raylib-build"
  "F:/Projekty/gamedev/rts-with-codex/build-verify/_deps/raylib-subbuild/raylib-populate-prefix"
  "F:/Projekty/gamedev/rts-with-codex/build-verify/_deps/raylib-subbuild/raylib-populate-prefix/tmp"
  "F:/Projekty/gamedev/rts-with-codex/build-verify/_deps/raylib-subbuild/raylib-populate-prefix/src/raylib-populate-stamp"
  "F:/Projekty/gamedev/rts-with-codex/build-verify/_deps/raylib-subbuild/raylib-populate-prefix/src"
  "F:/Projekty/gamedev/rts-with-codex/build-verify/_deps/raylib-subbuild/raylib-populate-prefix/src/raylib-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "F:/Projekty/gamedev/rts-with-codex/build-verify/_deps/raylib-subbuild/raylib-populate-prefix/src/raylib-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "F:/Projekty/gamedev/rts-with-codex/build-verify/_deps/raylib-subbuild/raylib-populate-prefix/src/raylib-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
