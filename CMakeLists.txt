cmake_minimum_required(VERSION 3.25)
project(melee_web LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static dependencies" FORCE)

# Public artifacts must not carry a developer checkout path through __FILE__
# panic strings or compiler metadata. This is opt-in for the public Release
# configure, leaving debug/replay builds byte-for-byte on their existing path
# policy while covering the native sources and pinned dependencies compiled in
# this directory.
option(MELEE_WEB_PUBLIC_RUNTIME "Redact local paths from public artifacts" OFF)
option(MELEE_WEB_AUDIO_PREVIEW_RUNTIME "Build the Release audio-enabled staging runtime" OFF)
option(MELEE_WEB_PIPELINE_PROVENANCE "Private pipeline-use coverage recorder" OFF)
option(MELEE_WEB_SELECTIVE_PIPELINES "Prepare certified descriptor unions before source interaction" OFF)
if(MELEE_WEB_AUDIO_PREVIEW_RUNTIME AND NOT MELEE_WEB_PUBLIC_RUNTIME)
  message(FATAL_ERROR "Audio preview runtime must use the redacted public source boundary")
endif()
if(MELEE_WEB_SELECTIVE_PIPELINES)
  if(MELEE_WEB_PIPELINE_PROVENANCE)
    message(FATAL_ERROR "Capture the complete seed separately from selective preparation")
  endif()
  add_compile_definitions(MELEE_WEB_SELECTIVE_PIPELINES=1)
endif()
if(MELEE_WEB_PIPELINE_PROVENANCE)
  if(MELEE_WEB_PUBLIC_RUNTIME)
    message(FATAL_ERROR "Private pipeline provenance must not be compiled into public artifacts")
  endif()
  add_compile_definitions(MELEE_WEB_PIPELINE_PROVENANCE=1)
endif()
if(EMSCRIPTEN AND MELEE_WEB_PUBLIC_RUNTIME)
  add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
  add_compile_options("-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.")
endif()

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

# The gameplay runtime is separate from the inspection renderer. Original
# source is copied at its pin and patched by scripts/gameplay_sources.py;
# gameplay-only ABI fixes must never silently alter the graphics dependency.
if(EMSCRIPTEN)
  set(MELEE_WEB_GAMEPLAY_SOURCE_DIR "" CACHE PATH "Verified generated Melee source tree")
  if(NOT EXISTS "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/ft/types.h")
    message(FATAL_ERROR "Generated gameplay source missing; use scripts/build.py")
  endif()
  add_library(gameplay_runtime STATIC src/gameplay_bootstrap.c src/gameplay_abi.c
    src/gameplay_ps_math.c
    src/gameplay_heap.cpp src/gameplay_fighter_probe.c src/gameplay_collision.c src/hsd_debug.c
    src/common_tables.c src/gameplay_fighter_attributes.c src/gameplay_fighter_data.c
    src/gameplay_article_data.c
    "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/ft/fighter.c"
    "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/ft/ftwalkcommon.c"
    "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/ft/ftparts.c"
    "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/ft/ftchangeparam.c"
    "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/mp/mpisland.c"
    "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/gr/ground.c"
    "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/gr/grdynamicattr.c"
    "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/lb/lb_00B0.c"
    # The common bootstrap initializes the original bone-dynamics pool before
    # any fighter exists. Its provider is also required by narrow HSD checks.
    "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/lb/lb_00F9.c")
  # Preserve this unit's original fighter-library numerical compilation policy
  # when moving its provider to the lower bootstrap dependency.
  set_source_files_properties("${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/lb/lb_00F9.c"
    PROPERTIES COMPILE_OPTIONS
    "-fno-builtin-sinf;-fno-builtin-cosf;-fno-builtin-tanf;-fno-builtin-atanf;-fno-builtin-atan2f;-fno-builtin-acosf")
  foreach(unit gobj gobjproc gobjplink gobjgxlink gobjobject gobjuserdata objalloc memory initialize)
    target_sources(gameplay_runtime PRIVATE "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/${unit}.c")
  endforeach()
  target_include_directories(gameplay_runtime PUBLIC src "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}")
  target_compile_definitions(gameplay_runtime PUBLIC TARGET_PC)
  target_compile_options(gameplay_runtime PRIVATE -ffunction-sections -fdata-sections -ffp-contract=off
    "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")
  target_link_libraries(gameplay_runtime PUBLIC aurora::os aurora::mtx PRIVATE fmt::fmt)
  # Native class construction retains original virtual callbacks. Link their
  # real SDK providers even when the diagnostic exercises load/free only.
  add_library(hsd_native_runtime STATIC src/hsd_texture_bounds.c src/hsd_native_camera.c src/hsd_native_arrays.c src/hsd_native_pobj.c src/hsd_native_joint.c src/hsd_native_common.c
    src/gameplay_stage_context.c src/gameplay_common_context.c "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/lb/lbspdisplay.c")
  foreach(unit class object hash id list jobj dobj mobj tobj texp texpdag tev
      displayfunc state aobj fobj robj wobj lobj spline mtx util quatlib
      bytecode perf random)
    target_sources(hsd_native_runtime PRIVATE "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/${unit}.c")
  endforeach()
  target_include_directories(hsd_native_runtime PUBLIC src "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}")
  target_compile_definitions(hsd_native_runtime PUBLIC TARGET_PC)
  target_compile_options(hsd_native_runtime PRIVATE -ffunction-sections -fdata-sections -ffp-contract=off
    -fno-builtin-sinf -fno-builtin-cosf -fno-builtin-tanf
    -fno-builtin-atanf -fno-builtin-atan2f -fno-builtin-acosf
    -include "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h")
  target_link_libraries(hsd_native_runtime PUBLIC gameplay_runtime aurora::core aurora::gx aurora::mtx aurora::vi)
  add_executable(gameplay_common_context_trace tests/gameplay_common_context_trace.c)
  target_link_libraries(gameplay_common_context_trace PRIVATE hsd_native_runtime)
  target_compile_options(gameplay_common_context_trace PRIVATE -ffp-contract=off
    -include "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h")
  target_link_options(gameplay_common_context_trace PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1
    -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
  set_target_properties(gameplay_common_context_trace PROPERTIES SUFFIX ".js")
  add_executable(hsd_native_trace tests/hsd_native_trace.c)
  target_link_libraries(hsd_native_trace PRIVATE hsd_native_runtime)
  target_compile_options(hsd_native_trace PRIVATE -ffp-contract=off
    -include "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h")
  target_link_options(hsd_native_trace PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1
    -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
  set_target_properties(hsd_native_trace PROPERTIES SUFFIX ".js")
  add_executable(hsd_native_dat_probe tests/hsd_native_dat_probe.cpp src/dat_archive.cpp
    src/dat_common.cpp src/dat_texture.cpp src/dat_material.cpp src/rigid_model.cpp src/dat_native_joint.cpp
    src/dat_material_animation.cpp src/dat_animation.cpp)
  target_link_libraries(hsd_native_dat_probe PRIVATE hsd_native_runtime)
  target_compile_options(hsd_native_dat_probe PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
  target_link_options(hsd_native_dat_probe PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
  set_target_properties(hsd_native_dat_probe PROPERTIES SUFFIX ".js")
  add_executable(gameplay_stage_lights_trace tests/gameplay_stage_lights_trace.cpp
    src/dat_lights.cpp src/dat_archive.cpp)
  target_link_libraries(gameplay_stage_lights_trace PRIVATE hsd_native_runtime)
  target_compile_options(gameplay_stage_lights_trace PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
  target_link_options(gameplay_stage_lights_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
  set_target_properties(gameplay_stage_lights_trace PROPERTIES SUFFIX ".js")
  add_executable(gameplay_scheduler_trace tests/gameplay_bootstrap_trace.c)
  add_executable(gameplay_abi_trace tests/gameplay_abi_trace.c)
  add_executable(gameplay_collision_trace tests/gameplay_collision_trace.c)
  foreach(target gameplay_scheduler_trace gameplay_abi_trace gameplay_collision_trace)
    target_link_libraries(${target} PRIVATE gameplay_runtime)
    target_compile_options(${target} PRIVATE -ffp-contract=off
      -include "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h")
    target_link_options(${target} PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1
      -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
    set_target_properties(${target} PROPERTIES SUFFIX ".js")
  endforeach()
  add_executable(gameplay_probe src/gameplay_probe.cpp src/dat_archive.cpp src/dat_common.cpp
    src/dat_collision.cpp src/dat_stage.cpp src/dat_fighter_runtime.cpp src/dat_fighter.cpp
    src/fighter_binding.cpp src/dat_animation.cpp)
  target_link_libraries(gameplay_probe PRIVATE gameplay_runtime)
  target_compile_options(gameplay_probe PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
  target_link_options(gameplay_probe PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1
    -sALLOW_MEMORY_GROWTH=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
  set_target_properties(gameplay_probe PROPERTIES SUFFIX ".js")
  include(cmake/FighterRuntime.cmake)
  add_custom_target(gameplay_checks DEPENDS gameplay_scheduler_trace gameplay_abi_trace gameplay_collision_trace gameplay_probe hsd_native_trace hsd_native_dat_probe gameplay_stage_lights_trace gameplay_common_context_trace)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/aurora/CMakeLists.txt")
  message(FATAL_ERROR "Dependencies missing. Run python3 scripts/bootstrap.py first.")
endif()
include(cmake/PinnedArchives.cmake)
add_subdirectory(.deps/aurora)
if(MELEE_WEB_SELECTIVE_PIPELINES AND MELEE_WEB_PUBLIC_RUNTIME)
  target_compile_definitions(aurora_gx PRIVATE MELEE_WEB_ALPHA_PIPELINE_FALLBACK=1)
endif()
if(MELEE_WEB_PIPELINE_PROVENANCE)
  target_sources(aurora_core PRIVATE src/pipeline_provenance.cpp src/pipeline_provenance_runtime.cpp)
  target_include_directories(aurora_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_include_directories(aurora_gx PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()
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
  src/hsd_material.cpp src/browser_input.cpp src/browser_controllers.cpp src/dat_animation.cpp src/dat_fighter.cpp
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
  configure_file(web/viewer.html viewer.html COPYONLY)
endif()

# Exercise the shipping input boundary without disc assets, game simulation,
# WebGPU initialization, audio, or pipeline preparation.
if(EMSCRIPTEN)
  add_executable(controller_probe EXCLUDE_FROM_ALL tests/controller_probe.cpp
    src/browser_input.cpp src/browser_controllers.cpp)
  target_include_directories(controller_probe PRIVATE src)
  target_link_libraries(controller_probe PRIVATE aurora::pad)
  target_compile_options(controller_probe PRIVATE -Wall -Wextra -Werror)
  target_link_options(controller_probe PRIVATE -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0
    -sEXPORTED_RUNTIME_METHODS=HEAP32,UTF8ToString
    -sEXPORTED_FUNCTIONS=_main,_controller_probe_sample,_melee_web_input_set_activity,_melee_web_input_set_keyboard,_melee_web_input_set_keyboard_port)
  set_target_properties(controller_probe PROPERTIES SUFFIX ".js")
  foreach(module controller-input controller-panel)
    configure_file(web/${module}.mjs ${module}.mjs COPYONLY)
  endforeach()
  configure_file(web/controller-panel.css controller-panel.css COPYONLY)
  configure_file(web/controller-check.html controller-check.html COPYONLY)
  configure_file(tests/controller-probe.html controller-probe.html COPYONLY)
endif()
