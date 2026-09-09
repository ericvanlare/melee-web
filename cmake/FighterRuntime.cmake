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
  src/gameplay_hud.c src/gameplay_menu.c src/gameplay_menu_host.c src/gameplay_item_runtime.c src/dat_item_commands.c src/gameplay_crowd.c src/gameplay_render.c src/gameplay_color_commands.c src/gameplay_match_rules.c src/gameplay_stage_visual.c src/gameplay_stage_map.c src/gameplay_stage_last.c src/gameplay_effect_runtime.c
  src/gameplay_audio.c src/gameplay_audio_bank_transport.c src/gameplay_audio_residency.c src/gameplay_audio_stream.c src/gameplay_io.cpp src/gameplay_audio_resample.c src/gameplay_audio_itd.c src/gameplay_audio_fx.c src/gameplay_audio_reverb.c
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/axfx/axfx.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/axfx/reverb_std.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/axfx/delay.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/ax/AXAlloc.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/ax/AXVPB.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/ax/AXCL.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/ax/AXAux.c" src/gameplay_sdk.c src/gameplay_action_store.c src/gameplay_bonus_data.c
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
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/sysdolphin/baselib/psdisp.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/MSL/float.c")
target_include_directories(fighter_source_runtime PUBLIC src "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}"
  PRIVATE .deps/aurora/include .deps/melee/extern/dolphin/include)
target_compile_definitions(fighter_source_runtime PUBLIC TARGET_PC PRIVATE MELEE_WEB_MENU_MARIO_FD)
set_source_files_properties(src/gameplay_platform.c PROPERTIES COMPILE_DEFINITIONS "MELEE_WEB_AUDIO;MELEE_WEB_AUDIO_STREAM")
set_source_files_properties(src/gameplay_audio.c PROPERTIES COMPILE_DEFINITIONS "MELEE_WEB_AUDIO_FX;MELEE_WEB_AUDIO_STREAM")
set_source_files_properties(
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/axfx/axfx.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/axfx/reverb_std.c"
  "${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/axfx/delay.c"
  PROPERTIES COMPILE_OPTIONS "-include;string.h;-include;math.h")
set_source_files_properties("${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/../extern/dolphin/src/dolphin/ax/AXCL.c" PROPERTIES COMPILE_OPTIONS "-include;string.h")
target_compile_options(fighter_source_runtime PRIVATE -ffunction-sections -fdata-sections -ffp-contract=off
  -include "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h")
target_link_libraries(fighter_source_runtime PUBLIC hsd_native_runtime aurora::pad)
add_library(fighter_asset_runtime STATIC EXCLUDE_FROM_ALL
  src/gameplay_audio_bank.cpp src/gameplay_audio_stream_asset.cpp src/dat_audio_stream.cpp src/dat_audio.cpp src/dat_audio_programs.cpp
  src/gameplay_hud_assets.cpp src/dat_scene.cpp src/gameplay_menu_world.cpp src/gameplay_match_session.cpp src/dat_menu_support.cpp src/dat_shape_animation.cpp src/dat_sis.cpp src/dat_native_menu.cpp src/dat_item_article.cpp src/gameplay_world.cpp src/dat_color_animation.cpp src/dat_native_stage.cpp src/dat_archive.cpp src/dat_common.cpp src/dat_native_joint.cpp src/rigid_model.cpp
  src/dat_texture.cpp src/dat_material.cpp src/dat_material_animation.cpp
  src/native_dat.cpp src/gameplay_fighter_assets.cpp src/gameplay_action_store.cpp
  src/dat_commands.cpp src/dat_fighter_runtime.cpp src/dat_fighter.cpp
  src/dat_animation.cpp src/fighter_binding.cpp src/dat_lights.cpp src/dat_collision.cpp src/dat_stage.cpp
  src/dat_item_registry.cpp src/dat_item_registry_native.cpp
  src/dat_effect_banks.cpp src/dat_effect_entries.cpp src/dat_native_animation.cpp)
target_link_libraries(fighter_asset_runtime PUBLIC fighter_source_runtime)
target_compile_options(fighter_asset_runtime PRIVATE -ffp-contract=off)
add_executable(fighter_runtime_probe EXCLUDE_FROM_ALL tests/fighter_runtime_probe.c tests/fighter_runtime_probe.cpp tests/gameplay_match_context_trace.c tests/gameplay_action_trace.c)
target_link_libraries(fighter_runtime_probe PRIVATE fighter_asset_runtime)
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

add_executable(gameplay_browser EXCLUDE_FROM_ALL src/gameplay_browser.cpp src/browser_input.cpp)
target_link_libraries(gameplay_browser PRIVATE fighter_asset_runtime aurora::main)
target_compile_options(gameplay_browser PRIVATE -ffp-contract=off)
target_link_options(gameplay_browser PRIVATE -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=8388608 -sEXIT_RUNTIME=0
  -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,addRunDependency,removeRunDependency,HEAPU8,UTF8ToString
  -lidbfs.js
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_melee_web_game_file,_melee_web_game_launch,_melee_web_game_launch_fixture,_melee_web_game_set_render_scale,_melee_web_game_set_stocks,_melee_web_game_unload,_melee_web_game_pause,_melee_web_game_message,_melee_web_game_stats,_melee_web_game_running,_melee_web_game_cache_idle,_melee_web_game_combat_check,_melee_web_game_stock_check,_melee_web_input_set_activity,_melee_web_input_set_keyboard,_melee_web_input_message,_melee_web_input_menu_buttons)
set_target_properties(gameplay_browser PROPERTIES SUFFIX ".js")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_link_options(gameplay_browser PRIVATE -sASSERTIONS=0 -sSAFE_HEAP=0)
  set(MELEE_WEB_BUILD_LABEL "Release")
else()
  target_link_options(gameplay_browser PRIVATE -sASSERTIONS=2 -sSAFE_HEAP=1)
  set(MELEE_WEB_BUILD_LABEL "checked RelWithDebInfo")
endif()
configure_file(web/runtime.html runtime.html @ONLY)
configure_file(web/runtime-cache.js runtime-cache.js COPYONLY)
foreach(module disc-image dsp-coefficients runtime-assets match-flow match-menu)
  configure_file(web/${module}.mjs ${module}.mjs COPYONLY)
endforeach()
configure_file(web/audio-ring.mjs audio-ring.mjs COPYONLY)
configure_file(web/audio-worklet.js audio-worklet.js COPYONLY)

add_executable(gameplay_audio_trace EXCLUDE_FROM_ALL tests/gameplay_audio_trace.cpp)
target_link_libraries(gameplay_audio_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_audio_trace PRIVATE -UNDEBUG)
target_link_options(gameplay_audio_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_audio_trace PROPERTIES SUFFIX ".js")

add_executable(gameplay_audio_fx_trace EXCLUDE_FROM_ALL tests/gameplay_audio_fx_trace.cpp)
target_link_libraries(gameplay_audio_fx_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_audio_fx_trace PRIVATE -UNDEBUG)
target_link_options(gameplay_audio_fx_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_audio_fx_trace PROPERTIES SUFFIX ".js")

add_executable(dat_native_stage_map_trace EXCLUDE_FROM_ALL
  tests/dat_native_stage_map_trace.cpp tests/dat_native_stage_map_trace.c)
target_link_libraries(dat_native_stage_map_trace PRIVATE fighter_asset_runtime)
target_compile_options(dat_native_stage_map_trace PRIVATE -UNDEBUG)
target_link_options(dat_native_stage_map_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(dat_native_stage_map_trace PROPERTIES SUFFIX ".js")

add_executable(gameplay_audio_stream_trace EXCLUDE_FROM_ALL tests/gameplay_audio_stream_trace.cpp)
target_link_libraries(gameplay_audio_stream_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_audio_stream_trace PRIVATE -UNDEBUG)
target_link_options(gameplay_audio_stream_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_audio_stream_trace PROPERTIES SUFFIX ".js")

add_executable(gameplay_stage_last_trace EXCLUDE_FROM_ALL tests/gameplay_stage_last_trace.cpp tests/gameplay_stage_last_trace.c)
target_link_libraries(gameplay_stage_last_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_stage_last_trace PRIVATE -UNDEBUG)
target_link_options(gameplay_stage_last_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_stage_last_trace PROPERTIES SUFFIX ".js")

add_executable(gameplay_stock_trace EXCLUDE_FROM_ALL tests/gameplay_stock_trace.cpp)
target_link_libraries(gameplay_stock_trace PRIVATE fighter_asset_runtime)
target_link_options(gameplay_stock_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_stock_trace PROPERTIES SUFFIX ".js")

add_executable(gameplay_edge_trace EXCLUDE_FROM_ALL tests/gameplay_edge_trace.cpp tests/gameplay_edge_state.c)
target_link_libraries(gameplay_edge_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_edge_trace PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")
target_link_options(gameplay_edge_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_edge_trace PROPERTIES SUFFIX ".js")

add_executable(gameplay_article_trace EXCLUDE_FROM_ALL
  tests/gameplay_article_trace.cpp tests/gameplay_article_item_state.c)
target_link_libraries(gameplay_article_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_article_trace PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")
target_link_options(gameplay_article_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_article_trace PROPERTIES SUFFIX ".js")

add_executable(gameplay_specials_trace EXCLUDE_FROM_ALL
  tests/gameplay_specials_trace.cpp tests/gameplay_article_item_state.c)
target_link_libraries(gameplay_specials_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_specials_trace PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")
target_link_options(gameplay_specials_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_specials_trace PROPERTIES SUFFIX ".js")

add_executable(gameplay_combat_trace EXCLUDE_FROM_ALL tests/gameplay_combat_trace.cpp)
target_link_libraries(gameplay_combat_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_combat_trace PRIVATE -ffp-contract=off)
target_link_options(gameplay_combat_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_combat_trace PROPERTIES SUFFIX ".js")

add_executable(gameplay_player_context_trace EXCLUDE_FROM_ALL tests/gameplay_player_context_trace.c)
target_link_libraries(gameplay_player_context_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_player_context_trace PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h")
target_link_options(gameplay_player_context_trace PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_player_context_trace PROPERTIES SUFFIX ".js")

# Emits original source state for comparison with read-only retail traces.
add_executable(gameplay_trajectory_trace EXCLUDE_FROM_ALL tests/gameplay_trajectory_trace.cpp tests/gameplay_trajectory_trace.c)
target_link_libraries(gameplay_trajectory_trace PRIVATE fighter_asset_runtime)
target_compile_options(gameplay_trajectory_trace PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")
target_link_options(gameplay_trajectory_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_SIZE=8388608)
set_target_properties(gameplay_trajectory_trace PROPERTIES SUFFIX ".js")

# Local owned-asset descriptor/lifetime probe; not an in-game menu acceptance test.
add_executable(native_menu_scene_trace EXCLUDE_FROM_ALL tests/native_menu_scene_trace.cpp tests/native_menu_scene_consumer.c tests/native_sis_consumer.c)
target_link_libraries(native_menu_scene_trace PRIVATE fighter_asset_runtime)
target_link_options(native_menu_scene_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=8388608)
set_target_properties(native_menu_scene_trace PROPERTIES SUFFIX ".js")
target_compile_options(native_menu_scene_trace PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")

# Runs the original SSS lifecycle using local, owned fixtures.
add_executable(native_sss_callbacks EXCLUDE_FROM_ALL tests/native_sss_callbacks.cpp tests/native_sss_callbacks.c)
target_link_libraries(native_sss_callbacks PRIVATE fighter_asset_runtime)
target_link_options(native_sss_callbacks PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=8388608 -Wl,--error-limit=0)
set_target_properties(native_sss_callbacks PROPERTIES SUFFIX ".js")
target_compile_options(native_sss_callbacks PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")

add_executable(dat_menu_support_trace EXCLUDE_FROM_ALL tests/dat_menu_support_test.cpp)
target_link_libraries(dat_menu_support_trace PRIVATE fighter_asset_runtime)
target_link_options(dat_menu_support_trace PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=8388608)
set_target_properties(dat_menu_support_trace PROPERTIES SUFFIX ".js")

# Local real CSS callback integration gate; no rendering/selection acceptance.
add_executable(native_css_callbacks EXCLUDE_FROM_ALL tests/native_css_callbacks.cpp tests/native_css_callbacks.c)
target_link_libraries(native_css_callbacks PRIVATE fighter_asset_runtime)
target_link_options(native_css_callbacks PRIVATE --profiling-funcs -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=8388608 -Wl,--error-limit=0)
set_target_properties(native_css_callbacks PROPERTIES SUFFIX ".js")
target_compile_options(native_css_callbacks PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")

# Real source SSM load/cancel/switch/publication and repeated ownership.
add_executable(native_audio_banks EXCLUDE_FROM_ALL tests/native_audio_banks.cpp tests/native_audio_banks.c)
target_link_libraries(native_audio_banks PRIVATE fighter_asset_runtime)
target_link_options(native_audio_banks PRIVATE --profiling-funcs -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=8388608 -Wl,--error-limit=0)
set_target_properties(native_audio_banks PROPERTIES SUFFIX ".js")
target_compile_options(native_audio_banks PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/gameplay_compat.h>")

add_executable(native_menu_host_trace EXCLUDE_FROM_ALL tests/native_menu_host_trace.cpp tests/native_menu_alarm_unavailable.c)
target_link_libraries(native_menu_host_trace PRIVATE fighter_asset_runtime)
target_link_options(native_menu_host_trace PRIVATE --profiling-funcs -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=8388608)
set_target_properties(native_menu_host_trace PROPERTIES SUFFIX ".js")

# Real original-menu browser integration; replaces temporary HTML selectors
# once the source CSS/SSS/match handoff passes acceptance.
add_executable(gameplay_menu_browser EXCLUDE_FROM_ALL src/gameplay_menu_browser.cpp src/browser_input.cpp tests/native_menu_alarm_unavailable.c)
target_link_libraries(gameplay_menu_browser PRIVATE fighter_asset_runtime aurora::main)
target_compile_options(gameplay_menu_browser PRIVATE -ffp-contract=off)
target_link_options(gameplay_menu_browser PRIVATE --profiling-funcs -sENVIRONMENT=web
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=8388608 -sEXIT_RUNTIME=0 -sASSERTIONS=2
  -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,addRunDependency,removeRunDependency,HEAPU8,UTF8ToString
  -lidbfs.js
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_melee_web_native_menu_file,_melee_web_native_menu_launch,_melee_web_native_menu_unload,_melee_web_native_menu_pause,_melee_web_native_menu_message,_melee_web_native_menu_running,_melee_web_native_menu_cache_idle,_melee_web_native_menu_phase,_melee_web_native_menu_confirm_check,_melee_web_native_menu_stock_check,_melee_web_native_menu_diagnostics,_melee_web_input_set_activity,_melee_web_input_set_keyboard,_melee_web_input_set_keyboard_port,_melee_web_input_message)
set_target_properties(gameplay_menu_browser PROPERTIES SUFFIX ".js")
configure_file(web/native-menu.html native-menu.html @ONLY)

# Shared typed scene/model tables consumed by the original match interface.
add_executable(dat_scene_trace EXCLUDE_FROM_ALL tests/dat_scene_test.cpp)
target_link_libraries(dat_scene_trace PRIVATE fighter_asset_runtime)
target_link_options(dat_scene_trace PRIVATE --profiling-funcs -sENVIRONMENT=node -sNODERAWFS=1
  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=8388608)
set_target_properties(dat_scene_trace PROPERTIES SUFFIX ".js")
