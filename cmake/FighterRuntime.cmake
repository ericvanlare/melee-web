# Original source closure for the fighter runtime gate. The explicit fighter
# build also supplies focused traces; execution uses user-owned local assets.
file(GLOB_RECURSE native_paths CONFIGURE_DEPENDS RELATIVE "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/*.c")
set(fighter_paths)
foreach(path IN LISTS native_paths)
  set(relative "${path}")
  # Existing checked HSD/runtime targets supply these translation units.
  if(relative STREQUAL "melee/gm/gmmain.c" OR relative STREQUAL "melee/db/dberror.c" OR relative MATCHES "^sysdolphin/baselib/" OR relative STREQUAL "melee/ft/fighter.c"
      OR relative STREQUAL "melee/ft/ft_0C8C.c" OR relative STREQUAL "melee/ft/ftCo_800C7CA0.c"
      OR relative STREQUAL "melee/ft/ftparts.c" OR relative STREQUAL "melee/ft/ftchangeparam.c"
      OR relative STREQUAL "melee/ft/ftwalkcommon.c" OR relative STREQUAL "melee/mp/mpisland.c" OR relative STREQUAL "melee/mp/mplib.c"
      OR relative STREQUAL "melee/gr/ground.c" OR relative STREQUAL "melee/gr/grdynamicattr.c"
      OR relative STREQUAL "melee/lb/lb_00B0.c" OR relative STREQUAL "melee/lb/lbspdisplay.c")
    continue()
  endif()
  list(APPEND fighter_paths "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/${relative}")
endforeach()
add_library(fighter_source_runtime STATIC EXCLUDE_FROM_ALL ${fighter_paths}
  src/gameplay_sdk.c src/gameplay_action_store.c src/gameplay_bonus_data.c
  src/gameplay_player_context.c src/gameplay_fighter_assets.c src/gameplay_effect_banks.c src/gameplay_ground_data.c src/gameplay_archive_sections.c src/gameplay_platform.c src/gameplay_match_context.c src/gameplay_font_atlas.c src/gameplay_stage_numeric.c src/dat_item_registry.c
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/particle.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/generator.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/hsd_3A64.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/hsd_3A94.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/hsd_3A76.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/hsd_3B27.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/hsd_3B2B.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/hsd_3B2E.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/hsd_4D11.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/sislib_font.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/archive.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/axdriver.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/controller.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/rumble.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/shadow.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/video.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/fog.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/synth.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/sislib.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/hsd_393C.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/psappsrt.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/psdisptev.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/MSL/float.c")
target_include_directories(fighter_source_runtime PUBLIC src "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}"
  PRIVATE .deps/aurora/include .deps/melee/extern/dolphin/include)
target_compile_definitions(fighter_source_runtime PUBLIC TARGET_PC)
target_compile_options(fighter_source_runtime PRIVATE -ffunction-sections -fdata-sections -ffp-contract=off
  -include "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h")
target_link_libraries(fighter_source_runtime PUBLIC hsd_native_runtime aurora::pad)
add_executable(fighter_runtime_probe EXCLUDE_FROM_ALL tests/fighter_runtime_probe.c tests/fighter_runtime_probe.cpp tests/gameplay_match_context_trace.c
  src/dat_archive.cpp src/dat_common.cpp src/dat_native_joint.cpp src/rigid_model.cpp
  src/dat_texture.cpp src/dat_material.cpp src/dat_material_animation.cpp
  src/native_dat.cpp src/gameplay_fighter_assets.cpp src/gameplay_action_store.cpp
  src/dat_commands.cpp src/dat_fighter_runtime.cpp src/dat_fighter.cpp
  src/dat_animation.cpp src/fighter_binding.cpp src/dat_lights.cpp src/dat_collision.cpp src/dat_stage.cpp
  src/dat_item_registry.cpp src/dat_item_registry_native.cpp
  src/dat_effect_banks.cpp src/dat_effect_entries.cpp src/dat_native_animation.cpp)
target_link_libraries(fighter_runtime_probe PRIVATE fighter_source_runtime)
target_compile_options(fighter_runtime_probe PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")
target_link_options(fighter_runtime_probe PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2
  -sSAFE_HEAP=1 -sSTACK_SIZE=8388608 -Wl,--error-limit=0)
set_target_properties(fighter_runtime_probe PROPERTIES SUFFIX ".js")

add_executable(gameplay_effect_banks_trace EXCLUDE_FROM_ALL tests/gameplay_effect_banks_trace.cpp
  src/dat_effect_banks.cpp src/native_dat.cpp src/dat_archive.cpp
  src/dat_effect_entries.cpp src/dat_native_animation.cpp src/dat_native_joint.cpp
  src/rigid_model.cpp src/dat_material.cpp src/dat_texture.cpp
  src/dat_material_animation.cpp src/dat_animation.cpp)
target_link_libraries(gameplay_effect_banks_trace PRIVATE fighter_source_runtime)
target_link_options(gameplay_effect_banks_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1
  -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_effect_banks_trace PROPERTIES SUFFIX ".js")

foreach(trace bonus_data stage_numeric)
  add_executable(gameplay_${trace}_trace EXCLUDE_FROM_ALL tests/gameplay_${trace}_trace.cpp
    src/native_dat.cpp src/dat_archive.cpp)
  target_link_libraries(gameplay_${trace}_trace PRIVATE fighter_source_runtime)
  target_link_options(gameplay_${trace}_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
    -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
  set_target_properties(gameplay_${trace}_trace PROPERTIES SUFFIX ".js")
endforeach()
