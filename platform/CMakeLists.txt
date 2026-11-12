add_library(platform_compat STATIC compat/ppc_intrinsics.c compat/cw_runtime.c)
target_include_directories(platform_compat INTERFACE compat/include)
target_include_directories(platform_compat PRIVATE ${PROJECT_SOURCE_DIR}/third_party/aurora/include)
target_compile_options(platform_compat INTERFACE "${XD_FLAG_FORCE_INCLUDE}${CMAKE_CURRENT_SOURCE_DIR}/compat/include/cw_compat.h")

add_library(platform_sdk STATIC sdk/gx_wgpipe.cpp sdk/gx_fifo.cpp sdk/gx_dump.cpp sdk/sdk_missing.c sdk/sdk_stubs.c sdk/os_threads.cpp sdk/vi_clock.cpp sdk/game_clock.cpp sdk/frame_cost.cpp sdk/cpu_cost.cpp sdk/gc_memory.cpp sdk/async_callbacks.cpp sdk/disc_drive.cpp sdk/card_serial.cpp sdk/card_backup.cpp sdk/card_root.cpp
  sdk/gx_render_modes.c sdk/mtx_ps.c)
target_include_directories(platform_sdk PRIVATE ${PROJECT_SOURCE_DIR}/third_party/aurora/lib
  ${PROJECT_SOURCE_DIR}/third_party/aurora/lib/dolphin/os)
target_include_directories(platform_sdk INTERFACE sdk/include)
target_link_libraries(platform_sdk PUBLIC aurora::gx aurora::core aurora::pad aurora::os aurora::dvd aurora::card
  engine_settings # system.disc_timing (async_callbacks.cpp)
  engine_memcard) # save backups before card writes (card_backup.cpp)
target_compile_options(platform_sdk INTERFACE "${XD_FLAG_FORCE_INCLUDE}${CMAKE_CURRENT_SOURCE_DIR}/sdk/include/sdk_port.h")

add_library(platform_dsp STATIC audio/dsp/dsp_decode.cpp audio/dsp/dsp_core.cpp audio/dsp/dsp_rom.cpp)
target_include_directories(platform_dsp PUBLIC audio/dsp/include)

add_library(platform_audio STATIC audio/gc_ai.cpp audio/gc_dsp_sdk.cpp audio/menu_music.cpp audio/port_sfx.cpp)
target_include_directories(platform_audio PRIVATE audio/musyx_sdk PUBLIC audio/include)
target_link_libraries(platform_audio PRIVATE platform_dsp ${AURORA_SDL3_TARGET})

add_library(platform_input STATIC input/keyboard_defaults.cpp input/input_script.cpp input/input_record.cpp
  input/input_record_format.cpp input/input_anchors.cpp input/controls_apply.cpp input/speedups.cpp)
target_include_directories(platform_input PUBLIC input/include)
target_link_libraries(platform_input PUBLIC engine_controls PRIVATE aurora::pad aurora::si platform_os)
target_link_libraries(platform_input PRIVATE aurora::core platform_video platform_audio)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(XD_PLATFORM_OS_SYSTEM_SOURCES os/crash_report_linux.cpp os/log_tee_linux.cpp os/run_status_linux.cpp
    os/os_small_linux.cpp)
elseif (APPLE)
  set(XD_PLATFORM_OS_SYSTEM_SOURCES os/crash_report_linux.cpp os/log_tee_linux.cpp os/run_status_linux.cpp
    os/os_small_mac.cpp)
  set_source_files_properties(os/crash_report_linux.cpp sdk/gc_memory.cpp sdk/os_threads.cpp audio/gc_ai.cpp
    PROPERTIES COMPILE_OPTIONS "${XD_MAC_COMPAT_OPTION}")
else ()
  set(XD_PLATFORM_OS_SYSTEM_SOURCES os/crash_report.cpp os/log_tee.cpp os/picture_jpeg.cpp os/run_status.cpp
    os/folder_claim.cpp os/window_icon.cpp)
endif ()
add_library(platform_os STATIC os/message.c os/zip_store.cpp os/user_folder.cpp os/disc_dialog.cpp
  ${XD_PLATFORM_OS_SYSTEM_SOURCES})
target_include_directories(platform_os PUBLIC os/include)
xd_link_windows_libs(platform_os PRIVATE dbghelp shell32 propsys ole32 windowscodecs)
target_link_libraries(platform_os PRIVATE ZLIB::ZLIB ${AURORA_SDL3_TARGET} borealis::data borealis::file_select)

add_library(platform_disc STATIC disc/disc.cpp disc/dol.c disc/disc_image.cpp disc/disc_overlay.cpp
  disc/disc_overlay_aurora.cpp)
target_include_directories(platform_disc PUBLIC disc/include)
target_link_libraries(platform_disc PRIVATE aurora::dvd) # brings nod (only visible inside Aurora)
if (WIN32)
  target_link_libraries(platform_disc PRIVATE bcrypt)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE)
  set(XD_WINDOW_SHOT_SOURCE video/window_shot_linux.cpp) # PNG through libpng (Aurora's copy)
else ()
  set(XD_WINDOW_SHOT_SOURCE video/window_shot.cpp)
endif ()
add_library(platform_video STATIC video/presentation.cpp video/photo_mode.cpp video/freeze_frame.cpp
  video/port_menu.cpp video/focus_pause.cpp ${XD_WINDOW_SHOT_SOURCE} video/gx_texture_decode.cpp video/button_art.cpp
  video/frame_capture.cpp video/fps_counter.cpp video/frame_rate.cpp)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE)
  target_link_libraries(platform_video PRIVATE PNG::PNG)
endif ()
target_include_directories(platform_video PUBLIC video/include)
target_link_libraries(platform_video PUBLIC aurora::core engine_controls PRIVATE aurora::gx aurora::vi aurora::pad engine_settings platform_os
  platform_audio ${AURORA_SDL3_TARGET})
xd_link_windows_libs(platform_video PRIVATE dwmapi windowscodecs ole32)

add_subdirectory(tests)
