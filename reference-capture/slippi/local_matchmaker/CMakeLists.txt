# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)
project(slippi_local_matchmaker LANGUAGES C CXX)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(SLIPPI_DOLPHIN_SOURCE "" CACHE PATH "Clean checkout of the pinned Slippi Dolphin source")
if(NOT IS_DIRECTORY "${SLIPPI_DOLPHIN_SOURCE}/Externals/enet/enet")
  message(FATAL_ERROR "SLIPPI_DOLPHIN_SOURCE must name the pinned Slippi Dolphin source tree")
endif()

execute_process(
  COMMAND git -C "${SLIPPI_DOLPHIN_SOURCE}" rev-parse HEAD
  OUTPUT_VARIABLE SLIPPI_DOLPHIN_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE GIT_RESULT
)
if(NOT GIT_RESULT EQUAL 0 OR
   NOT SLIPPI_DOLPHIN_COMMIT STREQUAL "41a7a3a110ed52999486ae1901c8fbb9a63d4f13")
  message(FATAL_ERROR
    "Expected project-slippi/dolphin commit 41a7a3a110ed52999486ae1901c8fbb9a63d4f13")
endif()

set(ENET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${SLIPPI_DOLPHIN_SOURCE}/Externals/enet/enet" "${CMAKE_CURRENT_BINARY_DIR}/enet")

set(LOCAL_MATCHMAKER_SOURCES pairing.cpp protocol.cpp browser_relay.cpp)
add_executable(slippi-local-matchmaker server.cpp ${LOCAL_MATCHMAKER_SOURCES})
target_compile_features(slippi-local-matchmaker PRIVATE cxx_std_20)
target_include_directories(slippi-local-matchmaker PRIVATE
  "${SLIPPI_DOLPHIN_SOURCE}/Externals/enet/enet/include"
  "${SLIPPI_DOLPHIN_SOURCE}/Externals"
)
target_link_libraries(slippi-local-matchmaker PRIVATE enet)

enable_testing()
add_executable(slippi-local-matchmaker-tests tests.cpp ${LOCAL_MATCHMAKER_SOURCES})
target_compile_features(slippi-local-matchmaker-tests PRIVATE cxx_std_20)
target_include_directories(slippi-local-matchmaker-tests PRIVATE
  "${SLIPPI_DOLPHIN_SOURCE}/Externals"
)
add_test(NAME slippi-local-matchmaker COMMAND slippi-local-matchmaker-tests)

add_executable(slippi-local-matchmaker-integration-test integration_test.cpp)
target_compile_features(slippi-local-matchmaker-integration-test PRIVATE cxx_std_20)
target_include_directories(slippi-local-matchmaker-integration-test PRIVATE
  "${SLIPPI_DOLPHIN_SOURCE}/Externals/enet/enet/include"
  "${SLIPPI_DOLPHIN_SOURCE}/Externals"
)
target_link_libraries(slippi-local-matchmaker-integration-test PRIVATE enet)
add_test(NAME slippi-local-matchmaker-integration
  COMMAND slippi-local-matchmaker-integration-test $<TARGET_FILE:slippi-local-matchmaker>)
set_tests_properties(slippi-local-matchmaker-integration PROPERTIES TIMEOUT 15)
add_test(NAME slippi-local-process
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
          -s "${CMAKE_CURRENT_SOURCE_DIR}/.." -p "test_*.py" -v)
set_tests_properties(slippi-local-process PROPERTIES TIMEOUT 15)
