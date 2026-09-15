cmake_minimum_required(VERSION 3.20)
project(webmelee_controller_probe LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The probe deliberately consumes the already-built SDL archive from the pinned
# Dolphin build.  No SDL source is added to this project and Dolphin is never
# rebuilt as part of this target.
set(SDL3_SOURCE_DIR "" CACHE PATH "Pinned SDL source directory")
set(SDL3_BUILD_DIR "" CACHE PATH "Pinned SDL build directory")
set(SDL3_LIBRARY "" CACHE FILEPATH "Pinned libSDL3.a")
set(LIBUSB_SOURCE_DIR "" CACHE PATH "Pinned bundled libusb source directory")
set(LIBUSB_LIBRARY "" CACHE FILEPATH "Pinned bundled libusb.a")

foreach(required_path IN ITEMS SDL3_SOURCE_DIR SDL3_BUILD_DIR LIBUSB_SOURCE_DIR SDL3_LIBRARY LIBUSB_LIBRARY)
  if(NOT EXISTS "${${required_path}}")
    message(FATAL_ERROR "${required_path} must point to an existing pinned SDL/Dolphin artifact")
  endif()
endforeach()

# SDL's exported static target names the bundled libusb target, but Dolphin's
# full build creates that target in its parent project.  Recreate only that
# imported edge here so the archive and its framework dependencies are linked
# exactly as in the existing macOS Dolphin build.
add_library(LibUSB::LibUSB STATIC IMPORTED GLOBAL)
set_target_properties(LibUSB::LibUSB PROPERTIES
  IMPORTED_LOCATION "${LIBUSB_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_SOURCE_DIR}/libusb")

# The pinned macOS libusb archive uses SecTask* for its entitlement checks.
# Dolphin's parent build adds this framework through its platform dependency
# graph; preserve that one edge in this standalone link so no dynamic or
# system SDL installation is consulted.
if(APPLE)
  set_property(TARGET LibUSB::LibUSB APPEND PROPERTY INTERFACE_LINK_LIBRARIES
    "$<LINK_ONLY:$<LINK_LIBRARY:FRAMEWORK,Security>>")
endif()

include("${SDL3_BUILD_DIR}/SDL3headersTargets.cmake")
include("${SDL3_BUILD_DIR}/SDL3staticTargets.cmake")

add_executable(webmelee-controller-probe main.cpp)
target_link_libraries(webmelee-controller-probe PRIVATE SDL3::SDL3-static)
set_target_properties(webmelee-controller-probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
