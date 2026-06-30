cmake_minimum_required(VERSION 3.16)

find_package(GTest CONFIG QUIET)

if (NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(rts_tests
    BalanceModifierTests.cpp
    BuildingConfigTests.cpp
    BuildingDomainTests.cpp
    BuildingPlacementTests.cpp
    GameCommandTests.cpp
    PlayerEconomyTests.cpp
    ResourceBufferTests.cpp
    ResearchCatalogTests.cpp
    RoadNetworkTests.cpp
    StrategicResourceTests.cpp
    TileMapDomainTests.cpp
    TechnologyTests.cpp
    WarSystemTests.cpp
)

target_link_libraries(rts_tests
    PRIVATE
        game
        GTest::gtest_main
        GTest::gmock
        Threads::Threads
)

if (WIN32)
    target_link_libraries(rts_tests PRIVATE winmm)
endif()

target_include_directories(rts_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
)

if (ENABLE_COVERAGE AND MSVC)
    target_compile_options(rts_tests PRIVATE /Od /Zi)
    target_link_options(rts_tests PRIVATE /DEBUG)
endif()

add_test(NAME rts_tests COMMAND rts_tests)
set_tests_properties(rts_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

if (ENABLE_COVERAGE AND MSVC)
    find_program(OPENCPPCOVERAGE_EXECUTABLE OpenCppCoverage OpenCppCoverage.exe)
    if (OPENCPPCOVERAGE_EXECUTABLE)
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/src" COVERAGE_SRC_DIR)
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/inc" COVERAGE_INC_DIR)
        file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/tests" COVERAGE_TESTS_DIR)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/coverage" COVERAGE_HTML_DIR)
        file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/coverage.xml" COVERAGE_XML_FILE)
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
            COMMAND ${OPENCPPCOVERAGE_EXECUTABLE}
                    --sources ${COVERAGE_SRC_DIR}
                    --sources ${COVERAGE_INC_DIR}
                    --excluded_sources ${COVERAGE_TESTS_DIR}
                    --export_type html:${COVERAGE_HTML_DIR}
                    --export_type cobertura:${COVERAGE_XML_FILE}
                    -- $<TARGET_FILE:rts_tests>
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS rts_tests
            COMMENT "Running unit tests and generating OpenCppCoverage report"
        )
    else()
        message(WARNING "OpenCppCoverage not found. Install it or pass -DOPENCPPCOVERAGE_EXECUTABLE=path/to/OpenCppCoverage.exe")
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E echo "OpenCppCoverage not found. Install it or configure with -DOPENCPPCOVERAGE_EXECUTABLE=path/to/OpenCppCoverage.exe"
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "Coverage requires OpenCppCoverage on Windows/MSVC"
        )
    endif()
elseif (ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    find_program(GCOVR_EXECUTABLE gcovr)
    if (GCOVR_EXECUTABLE)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${GCOVR_EXECUTABLE}
                    --root ${CMAKE_SOURCE_DIR}
                    --filter ${CMAKE_SOURCE_DIR}/src
                    --filter ${CMAKE_SOURCE_DIR}/inc
                    --exclude ${CMAKE_SOURCE_DIR}/tests
                    --print-summary
                    --html-details ${CMAKE_BINARY_DIR}/coverage.html
                    --xml ${CMAKE_BINARY_DIR}/coverage.xml
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            DEPENDS rts_tests
            COMMENT "Running unit tests and generating gcov coverage report"
        )
    else()
        message(WARNING "gcovr not found.")
    endif()
endif()
