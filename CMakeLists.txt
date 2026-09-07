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

add_library(hsd_state STATIC .deps/melee/src/sysdolphin/baselib/state.c src/hsd_pobj_bridge.c
  src/hsd_transform_bridge.c src/hsd_material_bridge.c src/hsd_host_support.c
  src/hsd_inspection_camera.c src/hsd_inspection_lights.c
  src/hsd_animation_bridge.c
  .deps/melee/src/sysdolphin/baselib/mtx.c .deps/melee/src/sysdolphin/baselib/mobj.c
  .deps/melee/src/sysdolphin/baselib/texp.c .deps/melee/src/sysdolphin/baselib/texpdag.c
  .deps/melee/src/sysdolphin/baselib/tev.c .deps/melee/src/sysdolphin/baselib/util.c
  .deps/melee/src/sysdolphin/baselib/displayfunc.c .deps/melee/src/sysdolphin/baselib/perf.c
  .deps/melee/src/sysdolphin/baselib/aobj.c .deps/melee/src/sysdolphin/baselib/fobj.c
  .deps/melee/src/sysdolphin/baselib/spline.c)
target_include_directories(hsd_state PRIVATE .deps/melee/src .deps/aurora/include)
target_compile_definitions(hsd_state PRIVATE TARGET_PC)
target_compile_options(hsd_state PRIVATE -ffunction-sections -fdata-sections
  -ffp-contract=off -include "${CMAKE_CURRENT_SOURCE_DIR}/src/hsd_probe_compat.h")

add_executable(gx_probe src/gx_probe.c src/hsd_probe.c src/dat_archive.cpp
  src/rigid_model.cpp src/asset_scene.cpp src/dat_texture.cpp src/dat_material.cpp
  src/hsd_material.cpp src/browser_input.cpp src/dat_animation.cpp src/dat_fighter.cpp
  src/dat_stage.cpp src/fighter_binding.cpp src/hsd_animation_bridge.cpp)
target_include_directories(gx_probe PRIVATE .deps/melee/src)
target_link_libraries(gx_probe PRIVATE hsd_state)
target_link_libraries(gx_probe PRIVATE aurora::core aurora::gx aurora::main aurora::vi aurora::mtx aurora::pad)
target_compile_options(gx_probe PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
if(EMSCRIPTEN)
  set_target_properties(gx_probe PROPERTIES SUFFIX ".js")
  target_link_options(gx_probe PRIVATE
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728
    -sSTACK_SIZE=8388608 -sASSERTIONS=1
    -sEXPORTED_RUNTIME_METHODS=FS,HEAPU8,UTF8ToString -sEXIT_RUNTIME=0
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_melee_web_asset_clear,_melee_web_asset_open,_melee_web_asset_symbol,_melee_web_asset_select,_melee_web_asset_message,_melee_web_input_set_activity,_melee_web_input_set_keyboard,_melee_web_input_message,_melee_web_asset_animation_open,_melee_web_asset_animation_play,_melee_web_asset_animation_playing,_melee_web_asset_animation_loaded,_melee_web_asset_animation_visible,_melee_web_asset_animation_message,_melee_web_asset_fighter_open,_melee_web_asset_fighter_message,_melee_web_asset_ready,_melee_web_asset_kind,_melee_web_asset_stage_count,_melee_web_asset_stage_select,_melee_web_asset_stage_message,_melee_web_asset_common_open,_melee_web_asset_common_message,_melee_web_asset_action_count,_melee_web_asset_action_name,_melee_web_asset_container_ready,_melee_web_asset_container_open,_melee_web_asset_action_select)
  configure_file(web/index.html index.html COPYONLY)
endif()
