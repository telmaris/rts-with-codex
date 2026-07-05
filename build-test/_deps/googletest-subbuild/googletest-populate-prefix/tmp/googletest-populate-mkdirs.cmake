# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "F:/Projekty/gamedev/rts-with-codex/build-test/_deps/googletest-src"
  "F:/Projekty/gamedev/rts-with-codex/build-test/_deps/googletest-build"
  "F:/Projekty/gamedev/rts-with-codex/build-test/_deps/googletest-subbuild/googletest-populate-prefix"
  "F:/Projekty/gamedev/rts-with-codex/build-test/_deps/googletest-subbuild/googletest-populate-prefix/tmp"
  "F:/Projekty/gamedev/rts-with-codex/build-test/_deps/googletest-subbuild/googletest-populate-prefix/src/googletest-populate-stamp"
  "F:/Projekty/gamedev/rts-with-codex/build-test/_deps/googletest-subbuild/googletest-populate-prefix/src"
  "F:/Projekty/gamedev/rts-with-codex/build-test/_deps/googletest-subbuild/googletest-populate-prefix/src/googletest-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "F:/Projekty/gamedev/rts-with-codex/build-test/_deps/googletest-subbuild/googletest-populate-prefix/src/googletest-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "F:/Projekty/gamedev/rts-with-codex/build-test/_deps/googletest-subbuild/googletest-populate-prefix/src/googletest-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
