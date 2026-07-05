# CMake generated Testfile for 
# Source directory: F:/Projekty/gamedev/rts-with-codex/tests
# Build directory: F:/Projekty/gamedev/rts-with-codex/build-test/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(rts_tests "F:/Projekty/gamedev/rts-with-codex/build-test/tests/Debug/rts_tests.exe")
  set_tests_properties(rts_tests PROPERTIES  WORKING_DIRECTORY "F:/Projekty/gamedev/rts-with-codex" _BACKTRACE_TRIPLES "F:/Projekty/gamedev/rts-with-codex/tests/CMakeLists.txt;51;add_test;F:/Projekty/gamedev/rts-with-codex/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(rts_tests "F:/Projekty/gamedev/rts-with-codex/build-test/tests/Release/rts_tests.exe")
  set_tests_properties(rts_tests PROPERTIES  WORKING_DIRECTORY "F:/Projekty/gamedev/rts-with-codex" _BACKTRACE_TRIPLES "F:/Projekty/gamedev/rts-with-codex/tests/CMakeLists.txt;51;add_test;F:/Projekty/gamedev/rts-with-codex/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(rts_tests "F:/Projekty/gamedev/rts-with-codex/build-test/tests/MinSizeRel/rts_tests.exe")
  set_tests_properties(rts_tests PROPERTIES  WORKING_DIRECTORY "F:/Projekty/gamedev/rts-with-codex" _BACKTRACE_TRIPLES "F:/Projekty/gamedev/rts-with-codex/tests/CMakeLists.txt;51;add_test;F:/Projekty/gamedev/rts-with-codex/tests/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(rts_tests "F:/Projekty/gamedev/rts-with-codex/build-test/tests/RelWithDebInfo/rts_tests.exe")
  set_tests_properties(rts_tests PROPERTIES  WORKING_DIRECTORY "F:/Projekty/gamedev/rts-with-codex" _BACKTRACE_TRIPLES "F:/Projekty/gamedev/rts-with-codex/tests/CMakeLists.txt;51;add_test;F:/Projekty/gamedev/rts-with-codex/tests/CMakeLists.txt;0;")
else()
  add_test(rts_tests NOT_AVAILABLE)
endif()
subdirs("../_deps/googletest-build")
