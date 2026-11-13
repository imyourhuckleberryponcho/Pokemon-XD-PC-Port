add_library(engine_formats STATIC formats/lzss_fsys.c formats/fsys_build.c formats/rel.c formats/data_table.c)
target_include_directories(engine_formats PUBLIC formats/include)

if (WIN32)
  add_library(engine_gs STATIC gs/gsthread_fibers.cpp)
else () # Linux: the same functions on ucontext, stacks below 2 GB (docs/LINUX.md)
  add_library(engine_gs STATIC gs/gsthread_ucontext.cpp)
  if (APPLE) # (docs/MAC.md: MAP_32BIT for macOS)
    set_source_files_properties(gs/gsthread_ucontext.cpp PROPERTIES COMPILE_OPTIONS "${XD_MAC_COMPAT_OPTION}")
  endif ()
endif ()
target_include_directories(engine_gs PUBLIC gs/include)

add_executable(gsthread_test gs/tests/gsthread_test.cpp)
target_link_libraries(gsthread_test PRIVATE engine_gs)

add_library(engine_settings STATIC settings/settings.cpp settings/settings_ini.cpp settings/standard_settings.cpp
  settings/settings_rules.cpp)
target_include_directories(engine_settings PUBLIC settings/include)

add_executable(settings_test settings/tests/settings_test.cpp)
target_link_libraries(settings_test PRIVATE engine_settings)

add_library(engine_controls STATIC controls/controls.cpp controls/controls_names.cpp controls/controls_settings.cpp)
target_include_directories(engine_controls PUBLIC controls/include)
target_link_libraries(engine_controls PUBLIC engine_settings)

add_executable(controls_test controls/tests/controls_test.cpp)
target_link_libraries(controls_test PRIVATE engine_controls)

add_library(engine_memcard STATIC memcard/memcard.cpp memcard/save_backups.cpp)
target_include_directories(engine_memcard PUBLIC memcard/include)

add_executable(memcard_test memcard/tests/memcard_test.cpp)
target_link_libraries(memcard_test PRIVATE engine_memcard)
add_executable(save_backups_test memcard/tests/save_backups_test.cpp)
target_link_libraries(save_backups_test PRIVATE engine_memcard)
add_library(engine_save_slots STATIC memcard/save_slots.cpp)
target_include_directories(engine_save_slots PRIVATE mods) # (json.h: a header by itself)
target_link_libraries(engine_save_slots PUBLIC engine_memcard)
target_compile_options(engine_save_slots PRIVATE ${XD_FLAG_UTF8})
add_executable(save_slots_test memcard/tests/save_slots_test.cpp)
target_link_libraries(save_slots_test PRIVATE engine_save_slots)
target_compile_options(save_slots_test PRIVATE ${XD_FLAG_UTF8})

add_library(engine_disc_check STATIC disc_check/disc_check.cpp disc_check/disc_check_cache.cpp)
target_include_directories(engine_disc_check PUBLIC disc_check/include)
target_link_libraries(engine_disc_check PUBLIC platform_disc)

add_executable(disc_check_test disc_check/tests/disc_check_test.cpp)
target_link_libraries(disc_check_test PRIVATE engine_disc_check)
target_compile_options(disc_check_test PRIVATE ${XD_FLAG_UTF8}) # (a test path with a non-English letter)
aurora_copy_runtime_dlls(disc_check_test)

add_library(engine_mods STATIC mods/mod_manifest.cpp mods/mod_list.cpp mods/mod_load.cpp mods/mod_review.cpp mods/mods_disc.cpp
  mods/data_mods.cpp)
target_include_directories(engine_mods PUBLIC mods/include)
target_link_libraries(engine_mods PUBLIC engine_formats platform_disc)

add_executable(mods_test mods/tests/mods_test.cpp)
target_link_libraries(mods_test PRIVATE engine_mods)
aurora_copy_runtime_dlls(mods_test)

add_executable(data_mods_test mods/tests/data_mods_test.cpp)
target_link_libraries(data_mods_test PRIVATE engine_mods)
target_compile_options(data_mods_test PRIVATE ${XD_FLAG_UTF8})
aurora_copy_runtime_dlls(data_mods_test)

add_library(engine_playthrough STATIC playthrough/playthrough.cpp)
target_include_directories(engine_playthrough PUBLIC playthrough/include PRIVATE mods)
target_link_libraries(engine_playthrough PUBLIC engine_mods engine_settings)
target_compile_options(engine_playthrough PRIVATE ${XD_FLAG_UTF8})

add_executable(playthrough_test playthrough/tests/playthrough_test.cpp)
target_link_libraries(playthrough_test PRIVATE engine_playthrough)
target_compile_options(playthrough_test PRIVATE ${XD_FLAG_UTF8})
aurora_copy_runtime_dlls(playthrough_test)

add_library(engine_gba_trade STATIC gba_trade/gen3_pokemon.cpp gba_trade/gen3_save.cpp)
target_include_directories(engine_gba_trade PUBLIC gba_trade/include)

add_executable(gen3_save_test gba_trade/tests/gen3_save_test.cpp)
target_link_libraries(gen3_save_test PRIVATE engine_gba_trade)

add_library(engine_settings_ui STATIC settings_ui/settings_ui.cpp)
target_include_directories(engine_settings_ui PUBLIC settings_ui/include)
target_link_libraries(engine_settings_ui PUBLIC engine_settings engine_controls imgui_headers)

add_library(monocypher STATIC ${PROJECT_SOURCE_DIR}/third_party/monocypher/monocypher.c
  ${PROJECT_SOURCE_DIR}/third_party/monocypher/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC ${PROJECT_SOURCE_DIR}/third_party/monocypher)
if (WIN32)
  add_library(engine_update STATIC update/update_manifest.cpp update/update_fetch.cpp update/update_install.cpp)
else () # Linux / Mac: no downloads yet (update/update_fetch_none.cpp answers "couldn't"); the launcher shows no updater
  add_library(engine_update STATIC update/update_manifest.cpp update/update_fetch_none.cpp update/update_install.cpp)
endif ()
target_include_directories(engine_update PUBLIC update/include)
target_link_libraries(engine_update PRIVATE monocypher ZLIB::ZLIB)
xd_link_windows_libs(engine_update PRIVATE winhttp bcrypt)

add_executable(update_test update/tests/update_test.cpp)
target_link_libraries(update_test PRIVATE engine_update ZLIB::ZLIB)
aurora_copy_runtime_dlls(update_test)

add_library(engine_randomizer STATIC randomizer/randomizer.cpp)
target_include_directories(engine_randomizer PUBLIC randomizer/include)

add_executable(randomizer_test randomizer/tests/randomizer_test.cpp)
target_link_libraries(randomizer_test PRIVATE engine_randomizer)
