
get_target_property(_imgui_dir imgui_headers INTERFACE_INCLUDE_DIRECTORIES)
add_library(launcher_imgui STATIC
  ${_imgui_dir}/imgui.cpp
  ${_imgui_dir}/imgui_draw.cpp
  ${_imgui_dir}/imgui_tables.cpp
  ${_imgui_dir}/imgui_widgets.cpp
  ${_imgui_dir}/misc/cpp/imgui_stdlib.cpp
  ${_imgui_dir}/backends/imgui_impl_sdl3.cpp
  ${_imgui_dir}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(launcher_imgui PUBLIC ${_imgui_dir})
target_link_libraries(launcher_imgui PUBLIC ${AURORA_SDL3_TARGET})

add_library(launcher_ui STATIC launcher_app.cpp launcher_icon.cpp launcher_reports.cpp launcher_extra.cpp)
if (WIN32)
  target_sources(launcher_ui PRIVATE launcher_sys_win.cpp)
else ()
  target_sources(launcher_ui PRIVATE launcher_sys_posix.cpp)
endif ()
target_include_directories(launcher_ui PUBLIC include)
target_link_libraries(launcher_ui PUBLIC engine_settings engine_controls engine_settings_ui engine_mods engine_update
  engine_memcard # the Save page's backups (save_backups.h)
  engine_save_slots # the Save page's "Saves" list (save_slots.h)
  launcher_imgui platform_os
  ${AURORA_SDL3_TARGET})
target_compile_options(launcher_ui PRIVATE ${XD_FLAG_UTF8})

set(LAUNCHER_FONT_DIR "${PROJECT_SOURCE_DIR}/third_party/fonts/inter" CACHE INTERNAL "")
function(launcher_copy_fonts target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LAUNCHER_FONT_DIR}/Inter-Regular.ttf"
      "${LAUNCHER_FONT_DIR}/Inter-SemiBold.ttf" "${LAUNCHER_FONT_DIR}/InterDisplay-Bold.ttf"
      "${LAUNCHER_FONT_DIR}/OFL.txt" "$<TARGET_FILE_DIR:${target}>/fonts/"
    VERBATIM)
endfunction()

add_executable(launcher_reports_test tests/reports_test.cpp launcher_reports.cpp)
target_include_directories(launcher_reports_test PRIVATE include)
target_link_libraries(launcher_reports_test PRIVATE platform_os ZLIB::ZLIB)
target_compile_options(launcher_reports_test PRIVATE ${XD_FLAG_UTF8})

add_executable(pbr_models_standin tests/pbr_models_standin.cpp)
add_executable(launcher_extra_test tests/extra_test.cpp)
add_dependencies(launcher_extra_test pbr_models_standin)
target_link_libraries(launcher_extra_test PRIVATE launcher_ui)
target_compile_options(launcher_extra_test PRIVATE ${XD_FLAG_UTF8})
aurora_copy_runtime_dlls(launcher_extra_test)
