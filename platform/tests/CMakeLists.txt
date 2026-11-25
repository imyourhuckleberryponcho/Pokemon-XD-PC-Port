
add_executable(gx_triangle gx_triangle.c)
target_link_libraries(gx_triangle PRIVATE aurora::core aurora::gx aurora::os aurora::main aurora::vi aurora::mtx)
aurora_copy_runtime_dlls(gx_triangle)

add_executable(gx_lines gx_lines.c)
target_link_libraries(gx_lines PRIVATE aurora::core aurora::gx aurora::os aurora::main aurora::vi aurora::mtx)
aurora_copy_runtime_dlls(gx_lines)
if (APPLE)
  target_link_options(gx_triangle PRIVATE "-Wl,-U,_MEM1Start")
  target_link_options(gx_lines PRIVATE "-Wl,-U,_MEM1Start")
endif ()

add_executable(gx_wgpipe gx_wgpipe.cpp)
target_link_libraries(gx_wgpipe PRIVATE platform_sdk aurora::core aurora::gx aurora::os aurora::main aurora::vi aurora::mtx)
aurora_copy_runtime_dlls(gx_wgpipe)

add_executable(gx_indexed gx_indexed.cpp)
target_link_libraries(gx_indexed PRIVATE platform_sdk aurora::core aurora::gx aurora::os aurora::main aurora::vi aurora::mtx)
aurora_copy_runtime_dlls(gx_indexed)

if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8) # (windows.h, psapi, MSVC link options: Windows only)
foreach (_variant normal low2gb lowimage)
  add_executable(addr_space_${_variant} addr_space.cpp)
  target_link_libraries(addr_space_${_variant} PRIVATE aurora::core aurora::gx aurora::os aurora::main aurora::vi psapi)
  aurora_copy_runtime_dlls(addr_space_${_variant})
endforeach ()
target_link_options(addr_space_low2gb PRIVATE /LARGEADDRESSAWARE:NO)
target_link_options(addr_space_lowimage PRIVATE /BASE:0x10000000 /DYNAMICBASE:NO)
endif ()

add_executable(disc_overlay_test disc_overlay_test.cpp)
target_link_libraries(disc_overlay_test PRIVATE platform_disc)
aurora_copy_runtime_dlls(disc_overlay_test)

add_executable(os_threads_test os_threads_test.cpp)
target_link_libraries(os_threads_test PRIVATE platform_sdk)
aurora_copy_runtime_dlls(os_threads_test)

add_executable(gc_memory_test gc_memory_test.cpp)
target_link_libraries(gc_memory_test PRIVATE platform_sdk aurora::core aurora::os aurora::main aurora::gx aurora::vi)
aurora_copy_runtime_dlls(gc_memory_test)

add_executable(low_alloc_ordinary_test low_alloc_ordinary_test.cpp)
target_link_libraries(low_alloc_ordinary_test PRIVATE platform_sdk aurora::core aurora::os aurora::main aurora::gx aurora::vi)
aurora_copy_runtime_dlls(low_alloc_ordinary_test)

add_executable(gx_fifo_test gx_fifo_test.cpp)
target_link_libraries(gx_fifo_test PRIVATE platform_sdk aurora::core aurora::gx aurora::os aurora::main aurora::vi)
aurora_copy_runtime_dlls(gx_fifo_test)

add_executable(time_stretch_test time_stretch_test.cpp)

add_executable(dsp_decode_test dsp_decode_test.cpp)
target_link_libraries(dsp_decode_test PRIVATE platform_dsp)

add_executable(dsp_core_test dsp_core_test.cpp)
target_link_libraries(dsp_core_test PRIVATE platform_dsp)

add_library(platform_dsp_reference STATIC dsp_reference/dsp_reference_core.cpp ../audio/dsp/dsp_rom.cpp)
target_compile_options(platform_dsp_reference PRIVATE
  "${XD_FLAG_FORCE_INCLUDE}${CMAKE_CURRENT_SOURCE_DIR}/dsp_reference/dsp_reference_names.h")
target_include_directories(platform_dsp_reference PUBLIC dsp_reference)
target_link_libraries(platform_dsp_reference PUBLIC platform_dsp)

add_executable(dsp_equiv_test dsp_equiv_test.cpp)
target_link_libraries(dsp_equiv_test PRIVATE platform_dsp platform_dsp_reference)

add_executable(async_callbacks_test async_callbacks_test.cpp)
target_link_libraries(async_callbacks_test PRIVATE platform_sdk aurora::dvd aurora::core aurora::os)
aurora_copy_runtime_dlls(async_callbacks_test)

add_executable(card_timing_test card_timing_test.cpp)
target_link_libraries(card_timing_test PRIVATE platform_sdk aurora::card aurora::dvd aurora::core aurora::os aurora::gx aurora::vi)
aurora_copy_runtime_dlls(card_timing_test)

add_executable(disc_drive_test disc_drive_test.cpp ../sdk/disc_drive.cpp)

if (WIN32) # (the test includes windows.h: not built on Linux / Mac)
add_executable(os_reset_test os_reset_test.cpp)
target_link_libraries(os_reset_test PRIVATE platform_sdk)
aurora_copy_runtime_dlls(os_reset_test)
endif ()

add_executable(background_controller_test background_controller_test.cpp)
target_link_libraries(background_controller_test PRIVATE ${AURORA_SDL3_TARGET})
aurora_copy_runtime_dlls(background_controller_test)

add_executable(keyboard_defaults_test keyboard_defaults_test.cpp)
target_link_libraries(keyboard_defaults_test PRIVATE platform_input aurora::pad aurora::core aurora::gx aurora::os)
aurora_copy_runtime_dlls(keyboard_defaults_test)

add_executable(input_record_test input_record_test.cpp)
target_link_libraries(input_record_test PRIVATE platform_input)

add_executable(controls_apply_test controls_apply_test.cpp)
target_link_libraries(controls_apply_test PRIVATE platform_input aurora::pad aurora::core aurora::gx aurora::os)
aurora_copy_runtime_dlls(controls_apply_test)
if (APPLE)
  target_link_options(keyboard_defaults_test PRIVATE "-Wl,-U,_MEM1Start")
  target_link_options(controls_apply_test PRIVATE "-Wl,-U,_MEM1Start")
endif ()

add_executable(game_clock_test game_clock_test.cpp ../sdk/game_clock.cpp)

if (WIN32) # (the test includes windows.h: not built on Linux / Mac)
add_executable(log_zip_test log_zip_test.cpp)
target_link_libraries(log_zip_test PRIVATE platform_os ZLIB::ZLIB)
endif ()

add_executable(battle_replay_file_test battle_replay_file_test.cpp)
target_link_libraries(battle_replay_file_test PRIVATE platform_os)

if (WIN32) # (the test includes windows.h: not built on Linux / Mac)
add_executable(folder_claim_test folder_claim_test.cpp)
target_link_libraries(folder_claim_test PRIVATE platform_os)
endif ()

if (WIN32) # (the test includes windows.h: not built on Linux / Mac)
add_executable(user_folder_test user_folder_test.cpp)
target_link_libraries(user_folder_test PRIVATE platform_os)
aurora_copy_runtime_dlls(user_folder_test)
endif ()

if (EXISTS "${XD_SNAPSHOT_DIR}/include/game/gc_fused.h")
  set(_gc_fused_dir "${XD_SNAPSHOT_DIR}/include")
else ()
  set(_gc_fused_dir "${PROJECT_SOURCE_DIR}/tools/fmadd/include")
endif ()
add_executable(gc_fused_test gc_fused_test.cpp gc_fused_test_c.c)
target_include_directories(gc_fused_test PRIVATE "${_gc_fused_dir}")

add_executable(button_art_test button_art_test.cpp)
target_link_libraries(button_art_test PRIVATE platform_video)
aurora_copy_runtime_dlls(button_art_test)

add_executable(xxh_dispatch_test xxh_dispatch_test.cpp)
target_include_directories(xxh_dispatch_test PRIVATE ${XD_AURORA_DIR}/lib)
target_link_libraries(xxh_dispatch_test PRIVATE aurora::gx)
aurora_copy_runtime_dlls(xxh_dispatch_test)
if (APPLE)
  target_link_options(xxh_dispatch_test PRIVATE "-Wl,-U,_MEM1Start")
endif ()

add_executable(dawn_bind_group_race_test dawn_bind_group_race_test.cpp
  ${XD_AURORA_DIR}/lib/webgpu/sampler_keeper.cpp)
target_include_directories(dawn_bind_group_race_test PRIVATE ${XD_AURORA_DIR}/lib)
target_link_libraries(dawn_bind_group_race_test PRIVATE dawn::webgpu_dawn)
aurora_copy_runtime_dlls(dawn_bind_group_race_test)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(folder_claim_posix_test folder_claim_posix_test.cpp)
  target_link_libraries(folder_claim_posix_test PRIVATE platform_os)
  add_executable(folder_claim_mac_test folder_claim_posix_test.cpp ../os/os_small_mac.cpp)
  target_compile_definitions(folder_claim_mac_test PRIVATE PORT_TEST_MAC_HALVES)
  target_include_directories(folder_claim_mac_test PRIVATE ../os/include)
  add_executable(mac_from_chars_test mac_from_chars_test.cpp)
  target_compile_definitions(mac_from_chars_test PRIVATE PORT_TEST_MAC_HALVES _GNU_SOURCE)
endif ()
if (APPLE)
  add_executable(low_arena_test low_arena_test.cpp ../os/low_arena.c ../os/low_malloc_mac.cpp ../os/low_malloc_selftest.cpp)
  target_compile_definitions(low_arena_test PRIVATE LOW_TEST_WITH_MALLOC)
  target_include_directories(low_arena_test PRIVATE ../os/include)
endif ()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(low_arena_test low_arena_test.cpp ../os/low_arena.c)
  target_include_directories(low_arena_test PRIVATE ../os/include)
  add_executable(low_malloc_mac_test low_arena_test.cpp ../os/low_arena.c ../os/low_malloc_mac.cpp ../os/low_malloc_selftest.cpp)
  target_compile_definitions(low_malloc_mac_test PRIVATE PORT_TEST_MAC_HALVES LOW_TEST_WITH_MALLOC)
  target_include_directories(low_malloc_mac_test PRIVATE ../os/include)
  add_executable(mac_compat_mmap_test mac_compat_mmap_test.c)
  target_compile_definitions(mac_compat_mmap_test PRIVATE PORT_TEST_MAC_HALVES)
endif ()
