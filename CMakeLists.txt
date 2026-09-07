cmake_minimum_required(VERSION 3.25)
project(melee_web LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static dependencies" FORCE)

# Keep this probe narrow. These services remain required for the game port.
set(AURORA_ENABLE_DVD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_THP OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_RMLUI OFF CACHE BOOL "" FORCE)
set(AURORA_CACHE_USE_ZSTD OFF CACHE BOOL "" FORCE)
set(AURORA_SDL3_PROVIDER vendor CACHE STRING "" FORCE)
if(EMSCRIPTEN)
  set(AURORA_DAWN_PROVIDER emscripten CACHE STRING "" FORCE)
  # Cross builds must not pick up native packages from a developer's machine.
  set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
  set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
  set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
  set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)
  set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY FALSE)
  set(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH FALSE)
  foreach(package absl fmt PNG Freetype SQLite3)
    set(CMAKE_DISABLE_FIND_PACKAGE_${package} TRUE)
  endforeach()
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "Use only pinned font dependencies" FORCE)
  add_compile_options(-fexceptions)
  add_link_options(-fexceptions -sASYNCIFY=1)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/aurora/CMakeLists.txt")
  message(FATAL_ERROR "Dependencies missing. Run python3 scripts/bootstrap.py first.")
endif()
include(cmake/PinnedArchives.cmake)
add_subdirectory(.deps/aurora)
if(EMSCRIPTEN)
  # Leave ZLIB discovery enabled for libpng's REQUIRED lookup of Aurora's
  # generated redirect, then verify it resolved to the pinned source build.
  get_target_property(zlib_source_target ZLIB::ZLIB ALIASED_TARGET)
  if(NOT zlib_source_target STREQUAL "zlib-ng")
    message(FATAL_ERROR "Expected vendored zlib-ng, got ${zlib_source_target}")
  endif()
endif()

add_library(hsd_state STATIC .deps/melee/src/sysdolphin/baselib/state.c src/hsd_pobj_bridge.c)
target_include_directories(hsd_state PRIVATE .deps/melee/src .deps/aurora/include)
target_compile_definitions(hsd_state PRIVATE TARGET_PC)
target_compile_options(hsd_state PRIVATE -ffunction-sections -fdata-sections
  -ffp-contract=off -include "${CMAKE_CURRENT_SOURCE_DIR}/src/hsd_probe_compat.h")

add_executable(gx_probe src/gx_probe.c src/hsd_probe.c src/dat_archive.cpp
  src/rigid_model.cpp src/asset_scene.cpp)
target_include_directories(gx_probe PRIVATE .deps/melee/src)
target_link_libraries(gx_probe PRIVATE hsd_state)
target_link_libraries(gx_probe PRIVATE aurora::core aurora::gx aurora::main aurora::vi)
target_compile_options(gx_probe PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
if(EMSCRIPTEN)
  set_target_properties(gx_probe PROPERTIES SUFFIX ".js")
  target_link_options(gx_probe PRIVATE
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728
    -sSTACK_SIZE=8388608 -sASSERTIONS=1
    -sEXPORTED_RUNTIME_METHODS=FS,HEAPU8,UTF8ToString -sEXIT_RUNTIME=0
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_melee_web_asset_clear,_melee_web_asset_open,_melee_web_asset_symbol,_melee_web_asset_select,_melee_web_asset_message)
  configure_file(web/index.html index.html COPYONLY)
endif()
