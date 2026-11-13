# PokÃ©mon XD game code, compiled from the current decomp snapshot.

if (XD_BUILD_GAME)
file(READ "${XD_SNAPSHOT_DIR}/objects.json" _xd_objects_json)
string(JSON _xd_count LENGTH "${_xd_objects_json}" objects)
math(EXPR _xd_last "${_xd_count} - 1")

set(XD_GAME_SOURCES "")
foreach (_i RANGE ${_xd_last})
  string(JSON _src GET "${_xd_objects_json}" objects ${_i} source)
  if (NOT _src MATCHES "^(game|musyx)/")
    continue()
  endif ()
  if (_src MATCHES "/dsp_import\\.c$")
    continue()
  endif ()
  if (_src MATCHES "/GSmovie/THP(Dec|Audio)\\.cpp$")
    continue()
  endif ()
  if (_src MATCHES "/GSmemman/memOverride\.cpp$")
    continue()
  endif ()
  set(_path "${XD_SNAPSHOT_DIR}/src/${_src}")
  if (NOT EXISTS "${_path}" AND EXISTS "${XD_SNAPSHOT_DIR}/extern/musyx/src/${_src}")
    set(_path "${XD_SNAPSHOT_DIR}/extern/musyx/src/${_src}")
  endif ()
  if (_src MATCHES "^musyx/")
    if (_src MATCHES "/runtime/stream\\.c$")
      set(XD_MUSYX_STREAM_C "${_path}")
      continue()
    endif ()
    list(APPEND XD_MUSYX_SOURCES "${_path}")
    continue()
  endif ()
  list(APPEND XD_GAME_SOURCES "${_path}")

  string(JSON _lang GET "${_xd_objects_json}" objects ${_i} lang)
  if (_lang STREQUAL "c")
    set_source_files_properties("${_path}" PROPERTIES LANGUAGE C)
  else ()
    set_source_files_properties("${_path}" PROPERTIES LANGUAGE CXX)
  endif ()

  string(JSON _n_inc LENGTH "${_xd_objects_json}" objects ${_i} includes)
  set(_incs "")
  if (_n_inc GREATER 0)
    math(EXPR _n_inc_last "${_n_inc} - 1")
    foreach (_j RANGE ${_n_inc_last})
      string(JSON _inc GET "${_xd_objects_json}" objects ${_i} includes ${_j})
      if (_inc MATCHES "^include/(hsd_ref)$")
        if (NOT WIN32) # (Linux: the system's own <sys/types.h> must win over hsd_ref's; see _xd_linux_first below)
          list(APPEND _incs "${CMAKE_BINARY_DIR}/linux_first")
        endif ()
        list(APPEND _incs "${XD_SNAPSHOT_DIR}/${_inc}")
      endif ()
    endforeach ()
  endif ()
  if (_incs)
    set_source_files_properties("${_path}" PROPERTIES INCLUDE_DIRECTORIES "${_incs}")
  endif ()
endforeach ()

option(XD_DISC_TABLES_VERIFY "Also compile the game's tables from the decomp's source and compare (test builds)" OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_xd_dt_dir "${CMAKE_BINARY_DIR}/disc_tables")
file(MAKE_DIRECTORY "${_xd_dt_dir}")
set(_xd_dt_sysinc "$ENV{INCLUDE}")
string(REPLACE "\\" "/" _xd_dt_sysinc "${_xd_dt_sysinc}")
string(REPLACE ";" "\", \"" _xd_dt_sysinc "${_xd_dt_sysinc}")
if (WIN32)
file(WRITE "${_xd_dt_dir}/flags.json.new" "{\"defines\": [\"AURORA\", \"AURORA_ENABLE_GX\", \"TARGET_PC=1\", \"WEBGPU_DAWN\", \"XD_PORT=1\", \"WIN32\", \"_WINDOWS\"],
 \"includes\": [\"${PROJECT_SOURCE_DIR}/platform/compat/include\", \"${PROJECT_SOURCE_DIR}/third_party/aurora/include\", \"${XD_SNAPSHOT_DIR}/include\", \"${PROJECT_SOURCE_DIR}/platform/sdk/include\"],
 \"force_includes\": [\"${CMAKE_CURRENT_SOURCE_DIR}/src/game_code_seg.h\", \"${PROJECT_SOURCE_DIR}/platform/compat/include/cw_compat.h\", \"${PROJECT_SOURCE_DIR}/platform/sdk/include/sdk_port.h\"],
 \"system_includes\": [\"${_xd_dt_sysinc}\"]}
")
elseif (APPLE)
  execute_process(COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir OUTPUT_VARIABLE _xd_clang_res
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(_xd_sdk "${CMAKE_OSX_SYSROOT}")
  if (NOT _xd_sdk OR NOT IS_DIRECTORY "${_xd_sdk}")
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-path OUTPUT_VARIABLE _xd_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif ()
  set(_xd_mac_sysinc "\"${_xd_sdk}/usr/include/c++/v1\", \"${_xd_clang_res}/include\", \"${_xd_sdk}/usr/include\"")
  set(_xd_linux_first "${CMAKE_BINARY_DIR}/linux_first")
  file(MAKE_DIRECTORY "${_xd_linux_first}/sys")
  file(WRITE "${_xd_linux_first}/sys/types.h.new" "#ifndef MSL_SYS_TYPES_H\n#define MSL_SYS_TYPES_H\n#include \"${_xd_sdk}/usr/include/sys/types.h\"\n#endif\n")
  file(COPY_FILE "${_xd_linux_first}/sys/types.h.new" "${_xd_linux_first}/sys/types.h" ONLY_IF_DIFFERENT)
  file(WRITE "${_xd_dt_dir}/flags.json.new" "{\"defines\": [\"AURORA\", \"AURORA_ENABLE_GX\", \"TARGET_PC=1\", \"WEBGPU_DAWN\", \"XD_PORT=1\", \"_XOPEN_SOURCE=700\", \"_DARWIN_C_SOURCE=1\"],
 \"includes\": [\"${PROJECT_SOURCE_DIR}/platform/compat/mac\", \"${PROJECT_SOURCE_DIR}/platform/compat/include\", \"${PROJECT_SOURCE_DIR}/third_party/aurora/include\", \"${XD_SNAPSHOT_DIR}/include\", \"${PROJECT_SOURCE_DIR}/platform/sdk/include\"],
 \"force_includes\": [\"${CMAKE_CURRENT_SOURCE_DIR}/src/game_code_seg.h\", \"${PROJECT_SOURCE_DIR}/platform/compat/include/cw_compat.h\", \"${PROJECT_SOURCE_DIR}/platform/sdk/include/sdk_port.h\"],
 \"first_includes\": [\"${_xd_linux_first}\"],
 \"system_includes\": [${_xd_mac_sysinc}], \"gc_system_includes\": [${_xd_mac_sysinc}],
 \"pc_target\": \"x86_64-apple-macosx13.3\", \"gc_target\": \"i386-apple-macosx10.13\"}
")
else ()
  if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "xd: on Linux the game code is built with Clang (presets linux-release / linux-debug)")
  endif ()
  execute_process(COMMAND "${CMAKE_C_COMPILER}" -print-resource-dir OUTPUT_VARIABLE _xd_clang_res
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(_xd_arch_inc "/usr/include/${CMAKE_LIBRARY_ARCHITECTURE}")
  file(GLOB _xd_cxx_inc "/usr/include/c++/*" "${_xd_arch_inc}/c++/*")
  set(_xd_gc_sysinc "")
  foreach (_d ${_xd_cxx_inc})
    string(APPEND _xd_gc_sysinc "\"${_d}\", ")
  endforeach ()
  string(APPEND _xd_gc_sysinc "\"${_xd_arch_inc}\"")
  set(_xd_linux_first "${CMAKE_BINARY_DIR}/linux_first")
  file(MAKE_DIRECTORY "${_xd_linux_first}/sys")
  file(WRITE "${_xd_linux_first}/sys/types.h.new" "#ifndef MSL_SYS_TYPES_H\n#define MSL_SYS_TYPES_H\n#include \"${_xd_arch_inc}/sys/types.h\"\n#endif\n")
  file(COPY_FILE "${_xd_linux_first}/sys/types.h.new" "${_xd_linux_first}/sys/types.h" ONLY_IF_DIFFERENT)
  file(WRITE "${_xd_dt_dir}/flags.json.new" "{\"defines\": [\"AURORA\", \"AURORA_ENABLE_GX\", \"TARGET_PC=1\", \"WEBGPU_DAWN\", \"XD_PORT=1\"],
 \"includes\": [\"${PROJECT_SOURCE_DIR}/platform/compat/include\", \"${PROJECT_SOURCE_DIR}/third_party/aurora/include\", \"${XD_SNAPSHOT_DIR}/include\", \"${PROJECT_SOURCE_DIR}/platform/sdk/include\"],
 \"force_includes\": [\"${CMAKE_CURRENT_SOURCE_DIR}/src/game_code_seg.h\", \"${PROJECT_SOURCE_DIR}/platform/compat/include/cw_compat.h\", \"${PROJECT_SOURCE_DIR}/platform/sdk/include/sdk_port.h\"],
 \"first_includes\": [\"${_xd_linux_first}\"],
 \"system_includes\": [\"${_xd_clang_res}/include\"], \"gc_system_includes\": [${_xd_gc_sysinc}],
 \"pc_target\": \"x86_64-pc-linux-gnu\", \"gc_target\": \"i686-pc-linux-gnu\"}
")
endif ()
file(COPY_FILE "${_xd_dt_dir}/flags.json.new" "${_xd_dt_dir}/flags.json" ONLY_IF_DIFFERENT)
set(_xd_dt_args --snapshot "${XD_SNAPSHOT_DIR}" --flags "${_xd_dt_dir}/flags.json" --out "${_xd_dt_dir}")
if (XD_DISC_TABLES_VERIFY)
  list(APPEND _xd_dt_args --verify)
endif ()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/gen_disc_tables.py" ${_xd_dt_args}
  RESULT_VARIABLE _xd_dt_result OUTPUT_VARIABLE _xd_dt_out ERROR_VARIABLE _xd_dt_err OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT _xd_dt_result EQUAL 0)
  message(FATAL_ERROR "xd: the game's tables (tools/gen_disc_tables.py, run with ${Python3_EXECUTABLE}) failed:\n"
    "${_xd_dt_out}\n${_xd_dt_err}\n"
    "(It needs Python with the libclang package: \"${Python3_EXECUTABLE}\" -m pip install libclang)")
endif ()
message(STATUS "xd: ${_xd_dt_out}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tools/gen_disc_tables.py"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/data/disc_table_addresses.tsv")
include("${_xd_dt_dir}/files.cmake")
foreach (_src ${XD_DISC_TABLE_SOURCES})
  list(REMOVE_ITEM XD_GAME_SOURCES "${XD_SNAPSHOT_DIR}/src/${_src}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${XD_SNAPSHOT_DIR}/src/${_src}")
endforeach ()
foreach (_w ${XD_DISC_TABLE_WRAPPERS} "${_xd_dt_dir}/disc_tables_list.c")
  if (_w MATCHES "\\.c$")
    set_source_files_properties("${_w}" PROPERTIES LANGUAGE C)
  endif ()
  set_property(SOURCE "${_w}" APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src")
endforeach ()
list(APPEND XD_GAME_SOURCES ${XD_DISC_TABLE_WRAPPERS} "${_xd_dt_dir}/disc_tables_list.c")

add_library(xd_game OBJECT ${XD_GAME_SOURCES})

if (DEFINED ENV{XD_GAME_NOOPT} AND NOT "$ENV{XD_GAME_NOOPT}" STREQUAL "")
  set(_noopt_count 0)
  foreach (_src ${XD_GAME_SOURCES})
    set(_skip FALSE)
    foreach (_re $ENV{XD_GAME_NOOPT_EXCEPT}) # (files that only link optimised, e.g. calls to inline-only helpers)
      if (_src MATCHES "${_re}")
        set(_skip TRUE)
      endif ()
    endforeach ()
    if (_skip)
      continue()
    endif ()
    foreach (_re $ENV{XD_GAME_NOOPT})
      if (_src MATCHES "${_re}")
        set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_OPTIONS ${XD_FLAGS_NOOPT})
        math(EXPR _noopt_count "${_noopt_count} + 1")
        break()
      endif ()
    endforeach ()
  endforeach ()
  message(STATUS "xd: XD_GAME_NOOPT: ${_noopt_count} game files without optimisation")
endif ()

set_property(SOURCE "${XD_SNAPSHOT_DIR}/src/game/main.cpp" APPEND PROPERTY COMPILE_DEFINITIONS main=XDGame_Main)

endif () # XD_BUILD_GAME

function(xd_use_decomp_headers target)
  target_include_directories(${target} PRIVATE
    ${PROJECT_SOURCE_DIR}/platform/compat/include
    ${PROJECT_SOURCE_DIR}/third_party/aurora/include
    ${XD_SNAPSHOT_DIR}/include
  )
  target_link_libraries(${target} PRIVATE platform_compat platform_sdk aurora::core aurora::gx aurora::os aurora::pad
    aurora::vi aurora::mtx aurora::card aurora::dvd)
  target_compile_definitions(${target} PRIVATE XD_PORT=1)
  target_compile_options(${target} PRIVATE ${XD_FLAG_UTF8} $<$<COMPILE_LANGUAGE:CXX>:${XD_FLAGS_OLD_CXX}>)
  target_compile_options(${target} PRIVATE ${XD_FLAGS_GAME_CODE}) # (Linux: the Windows build's struct layouts; top CMakeLists.txt)
endfunction()

if (XD_BUILD_GAME)
xd_use_decomp_headers(xd_game)
target_compile_options(xd_game PRIVATE ${XD_FLAG_NO_WARNINGS})
target_compile_options(xd_game PRIVATE ${XD_FLAG_NO_BUILTINS})
if (NOT MSVC)
  target_compile_options(xd_game PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-assume-sane-operator-new>)
  target_compile_options(xd_game PRIVATE $<$<COMPILE_LANGUAGE:C>:-Dstatic_assert=_Static_assert>)
endif ()
target_compile_options(xd_game PRIVATE "${XD_FLAG_FORCE_INCLUDE}${CMAKE_CURRENT_SOURCE_DIR}/src/game_code_seg.h")
target_compile_definitions(xd_game PRIVATE PADRead=PortInput_PADRead SIProbe=PortInput_SIProbe)
target_compile_definitions(xd_game PRIVATE PSVECSquareMag=PortPS_VECSquareMag PSVECMag=PortPS_VECMag
  PSVECNormalize=PortPS_VECNormalize PSVECSquareDistance=PortPS_VECSquareDistance PSVECDistance=PortPS_VECDistance)
foreach (_src ${XD_GAME_SOURCES})
  if (_src MATCHES "/GSgfxM/render\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS GSgfxPerfbarCPUEnd=PortXD_PerfbarCPUEnd
      GSgfxPerfbarCPUStart=PortXD_PerfbarCPUStart GSgfxPerfbarCPUEntry=PortXD_PerfbarCPUEntry)
  endif ()
endforeach ()
foreach (_src ${XD_GAME_SOURCES})
  if (_src MATCHES "/GSfsys/GSfsys2\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS _fsysFreeBlock=PortXD_fsysFreeBlock)
  endif ()
endforeach ()
foreach (_src ${XD_GAME_SOURCES})
  if (_src MATCHES "/GSdvd/GSdvd\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS PortDVD_Open=PortXD_DVDOpen
      DVDConvertPathToEntrynum=PortXD_DVDConvertPathToEntrynum)
  endif ()
endforeach ()
foreach (_src ${XD_GAME_SOURCES})
  if (_src MATCHES "/GSmodel/bound\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS
      GSgfxParseDisplayList=PortXD_BoundParseDisplayList GSboundAddPoint=PortXD_BoundAddPoint)
  endif ()
endforeach ()
foreach (_src ${XD_GAME_SOURCES})
  if (_src MATCHES "/GSgfxM/perfbar\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS OSGetTime=PortTime_GamePerfGetTime
      GSgfxPerfbarCPUEntry=GSgfxPerfbarCPUEntry_Game) # every marker goes through src/perf_marks.cpp
  elseif (_src MATCHES "/game/pokecolo\\.cpp$" OR _src MATCHES "/memcard/memcard\\.cpp$" OR
          _src MATCHES "/GSmath/init\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS
      OSGetTime=PortTime_GameGetRealTime OSGetTick=PortTime_GameGetTick)
  else ()
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS
      OSGetTime=PortTime_GameGetTime OSGetTick=PortTime_GameGetTick)
  endif ()
endforeach ()

foreach (_src ${XD_GAME_SOURCES})
  set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS GXBegin=PortXD_GXBegin
    GXCallDisplayList=PortXD_GXCallDisplayList GXLoadTexObj=PortXD_GXLoadTexObj DCFlushRange=PortXD_DCFlushRange
    DCStoreRange=PortXD_DCStoreRange DCFlushRangeNoSync=PortXD_DCFlushRangeNoSync
    DCStoreRangeNoSync=PortXD_DCStoreRangeNoSync DCInvalidateRange=PortXD_DCInvalidateRange)
  if (_src MATCHES "/app/floor/floor\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS GSmemGetHeapFreeSize=PortXD_FloorHeapFreeSize)
  endif ()
  if (_src MATCHES "/GSmemman/OSAlloc\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS OSInitAlloc=PortHeapReal_OSInitAlloc
      OSCreateHeap=PortHeapReal_OSCreateHeap OSCreateExternalHeap=PortHeapReal_OSCreateExternalHeap
      OSDestroyHeap=PortHeapReal_OSDestroyHeap OSAllocFromHeap=PortHeapReal_OSAllocFromHeap
      OSFreeToHeap=PortHeapReal_OSFreeToHeap OSShrinkHeap=PortHeapReal_OSShrinkHeap
      OSSetAllocMethodHeap=PortHeapReal_OSSetAllocMethodHeap OSGetHeapFreeSize=PortHeapReal_OSGetHeapFreeSize)
  elseif (_src MATCHES "/GSmemman/poolAlloc\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS GSmemCreatePool=PortHeapReal_GSmemCreatePool
      GSmemGetFreePool=PortHeapReal_GSmemGetFreePool GSmemAllocPool=PortHeapReal_GSmemAllocPool
      GSmemFreePool=PortHeapReal_GSmemFreePool)
  elseif (_src MATCHES "/GSmemman/sysdolmem\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS
      GSmemInitSysdolAllocator=PortHeapReal_GSmemInitSysdolAllocator
      GSmemGetSysdolAllocator=PortHeapReal_GSmemGetSysdolAllocator)
  elseif (_src MATCHES "/baselib/memory\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS HSD_Alloc=PortHeapReal_HSD_Alloc
      HSD_Free=PortHeapReal_HSD_Free)
  elseif (_src MATCHES "/baselib/objalloc\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS HSD_ObjAlloc=PortHeapReal_HSD_ObjAlloc
      HSD_ObjAllocInit=PortHeapReal_HSD_ObjAllocInit)
  endif ()
endforeach ()

set(_xd_tick_gsinput GSinputButtonsPressed GSinputButtonsChanged GSinputButtonsRepeat GSinputGetLeftStickXData
  GSinputGetLeftStickYData GSinputGetRightStickXData GSinputGetRightStickYData)
set(_xd_tick_window windowGetKeyInfo windowGetKeyInfo2 windowGetKeyRepeat windowGetKeyOn windowGetKeyTrg)
set(_xd_tick_portkey windowGetPortKeyInfo windowGetPortKeyInfo2)
foreach (_src ${XD_GAME_SOURCES})
  set(_defs "")
  if (NOT _src MATCHES "/GSinput/GSinput\\.cpp$" AND NOT _src MATCHES "/pokecolo\\.cpp$" AND
      NOT _src MATCHES "/app/menu/menu\\.cpp$" AND NOT _src MATCHES "/dbgMenuGSvtr\\.cpp$" AND
      NOT _src MATCHES "/app/light/light\\.cpp$")
    foreach (_n ${_xd_tick_gsinput})
      list(APPEND _defs "${_n}=PortTick_${_n}")
    endforeach ()
  endif ()
  if (_src MATCHES "/app/menu/menu\\.cpp$")
    list(APPEND _defs "windowGetKeyInfo=PortTick_menu_windowGetKeyInfo" "windowGetKeyInfo2=PortTick_windowGetKeyInfo2")
  elseif (NOT _src MATCHES "/app/menu/window\\.cpp$")
    foreach (_n ${_xd_tick_window})
      list(APPEND _defs "${_n}=PortTick_${_n}")
    endforeach ()
  endif ()
  if (NOT _src MATCHES "/app/menu/window\\.cpp$")
    if (NOT _src MATCHES "/app/menu/menu\\.cpp$")
      foreach (_n ${_xd_tick_portkey})
        list(APPEND _defs "${_n}=PortTick_${_n}")
      endforeach ()
    endif ()
  endif ()
  if (NOT _src MATCHES "/app/menu/menu\\.cpp$")
    list(APPEND _defs "menuGetKeyInfo=PortTick_menuGetKeyInfo")
  endif ()
  if (_src MATCHES "/pokecolo\\.cpp$" OR _src MATCHES "/menuLogoDemo\\.cpp$")
    list(APPEND _defs "GSinputReadStart=PortTick_GSinputReadStart")
  endif ()
  if (_src MATCHES "/msgctrl/msgctrlcode\\.c$")
    list(APPEND _defs "msgctrlKeyWait=PortTick_msgctrlKeyWait")
  endif ()
  if (_src MATCHES "/GSmsg/GSmsg\\.cpp$")
    list(APPEND _defs "spriteSetEnv=PortXD_MsgSpriteSetEnv" "timeGetLastFrameTime=PortXD_MsgFrameTime")
  endif ()
  if (_src MATCHES "/fightSeq/fightSeq\\.cpp$" OR _src MATCHES "/fightSeq/fightSeqBasis\\.cpp$")
    list(APPEND _defs "wazaCheckDarkWaza=PortXD_SplitCheckDarkWaza" "zokuseiBiosGetWazaKind=PortXD_SplitTypeKind")
  endif ()
  if (_src MATCHES "/GSmodel/GSmodel\\.cpp$")
    list(APPEND _defs "GSboundIsInViewFast=PortXD_ModelBoundInView" "modelShadowUpdate=PortXD_ModelShadowUpdate"
      "GSgfxPerfbarCPUEntry=PortXD_ModelPerfEntry")
  elseif (_src MATCHES "/GSparticle/peBank\\.cpp$")
    list(APPEND _defs "GSboundIsInViewFast=PortXD_DrawBoundInView")
  elseif (_src MATCHES "/GSmsg/sprite\\.cpp$")
    list(APPEND _defs "spriteSetEnv=PortXD_RealSpriteSetEnv" "spriteSetScissor=PortXD_RealSpriteSetScissor")
  elseif (_src MATCHES "/app/menu/menu\\.cpp$")
    list(APPEND _defs "spriteSetOffset=PortXD_MenuSpriteSetOffset")
  endif ()
  if (_src MATCHES "/fight/fightMenu\\.cpp$") # the battle command menu opens (an anchor)
    list(APPEND _defs "menuFightOpenTop=PortTick_menuFightOpenTop")
  endif ()
  set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS ${_defs})
endforeach ()

foreach (_src ${XD_GAME_SOURCES})
  if (_src MATCHES "/app/menu/menu\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS winSpriteDraw=PortXD_MenuWinSpriteDraw)
  elseif (_src MATCHES "/game/menuField\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS GSmsgPrint2=PortXD_FieldMenuPrint2)
  elseif (_src MATCHES "/app/fight/fightMenu\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS menuPocket2Open=PortXD_FightPocketOpen)
  elseif (_src MATCHES "/fightSeq/fightSeqBasis\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS fightOutPokemonKizetuEffect=PortXD_KizetuEffect)
  elseif (_src MATCHES "/fightSeq/fightSeqItemseq\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS fightOutPokemonHokakuEffect=PortXD_HokakuEffect
                 fightPokemon_SetHokakuFlag=PortXD_HokakuFlag)
  elseif (_src MATCHES "/game/menuTop\\.cpp$") # the title menu: its description line (the same entry there)
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS GSmsgPrint2=PortXD_TitleMenuPrint2)
  elseif (_src MATCHES "/GSsnd/GSsnd\\.cpp$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS sndPushGroup=PortXD_SndPushGroup)
  endif ()
endforeach ()

if (NOT XD_MUSYX_STREAM_C)
  message(FATAL_ERROR "MusyX's stream.c is not in the snapshot's file list (platform/audio/musyx_categories.c needs it)")
endif ()
foreach (_src ${XD_MUSYX_SOURCES})
  if (_src MATCHES "/runtime/synth\\.c$")
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS hwSetVolume=PortMusyX_SynthSetVolume)
  endif ()
endforeach ()
set(_xd_musyx_categories "${PROJECT_SOURCE_DIR}/platform/audio/musyx_categories.c")
list(APPEND XD_MUSYX_SOURCES "${_xd_musyx_categories}")
get_filename_component(_xd_musyx_src_root "${XD_MUSYX_STREAM_C}/../../.." ABSOLUTE) # .../extern/musyx/src
set_property(SOURCE "${_xd_musyx_categories}" APPEND PROPERTY INCLUDE_DIRECTORIES "${_xd_musyx_src_root}"
  "${PROJECT_SOURCE_DIR}/platform/audio/include")
set(_xd_musyx_ghost "${PROJECT_SOURCE_DIR}/platform/audio/musyx_ghost.c")
list(APPEND XD_MUSYX_SOURCES "${_xd_musyx_ghost}")
set_property(SOURCE "${_xd_musyx_ghost}" APPEND PROPERTY INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/platform/audio/include")
add_library(xd_musyx OBJECT ${XD_MUSYX_SOURCES})
target_compile_options(xd_musyx PRIVATE "${XD_FLAG_FORCE_INCLUDE}${PROJECT_SOURCE_DIR}/platform/audio/musyx_sections.h")
if (NOT MSVC)
  set(_xd_musyx_linux "${CMAKE_BINARY_DIR}/musyx_linux")
  file(READ "${XD_SNAPSHOT_DIR}/extern/musyx/include/musyx/musyx.h" _xd_musyx_h)
  string(FIND "${_xd_musyx_h}" "typedef unsigned long long u64;" _xd_found)
  if (_xd_found EQUAL -1)
    message(FATAL_ERROR "xd: musyx.h no longer has the line 'typedef unsigned long long u64;' (games/xd/CMakeLists.txt)")
  endif ()
  string(REPLACE "typedef unsigned long long u64;" "typedef uint64_t u64;" _xd_musyx_h "${_xd_musyx_h}")
  file(MAKE_DIRECTORY "${_xd_musyx_linux}/musyx")
  file(WRITE "${_xd_musyx_linux}/musyx/musyx.h.new" "${_xd_musyx_h}")
  file(COPY_FILE "${_xd_musyx_linux}/musyx/musyx.h.new" "${_xd_musyx_linux}/musyx/musyx.h" ONLY_IF_DIFFERENT)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${XD_SNAPSHOT_DIR}/extern/musyx/include/musyx/musyx.h")
  set(XD_MUSYX_LINUX_OPTIONS "-include${PROJECT_SOURCE_DIR}/platform/audio/musyx_linux_long.h"
    -fno-zero-initialized-in-bss ${XD_FLAGS_GAME_CODE} -fno-builtin
    -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=incompatible-function-pointer-types
    -Wno-error=incompatible-pointer-types)
  target_include_directories(xd_musyx PRIVATE "${_xd_musyx_linux}")
  target_compile_options(xd_musyx PRIVATE ${XD_MUSYX_LINUX_OPTIONS})
  foreach (_src ${XD_MUSYX_SOURCES})
    if (_src MATCHES "/runtime/hw_dolphin\\.c$")
      set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_OPTIONS "-DsalInitDsp(...)=salInitDsp(u32 portUnused)")
    endif ()
  endforeach ()
endif ()
target_include_directories(xd_musyx PRIVATE "${XD_SNAPSHOT_DIR}/extern/musyx/include"
  ${PROJECT_SOURCE_DIR}/platform/audio/musyx_sdk ${PROJECT_SOURCE_DIR}/platform/compat/include)
target_link_libraries(xd_musyx PRIVATE platform_compat)
target_compile_definitions(xd_musyx PRIVATE MUSY_TARGET=MUSY_TARGET_DOLPHIN MUSY_VERSION_MAJOR=2
  MUSY_VERSION_MINOR=0 MUSY_VERSION_PATCH=3 _USE_MATH_DEFINES)
set_property(TARGET xd_musyx PROPERTY C_STANDARD 99)
target_compile_options(xd_musyx PRIVATE ${XD_FLAG_NO_WARNINGS})
target_compile_options(xd_musyx PRIVATE ${XD_FLAG_UNDEF_DEBUG})
endif () # XD_BUILD_GAME

add_library(xd_port STATIC src/dol_tables.c src/data/common_rel.cpp)
target_include_directories(xd_port PUBLIC src src/data)
target_link_libraries(xd_port PUBLIC platform_disc engine_formats)
xd_use_decomp_headers(xd_port)

add_executable(xd_disc_check tools/xd_disc_check.c)
target_link_libraries(xd_disc_check PRIVATE xd_port)
aurora_copy_runtime_dlls(xd_disc_check)

add_executable(xd_dsp_test tools/xd_dsp_test.cpp)
target_link_libraries(xd_dsp_test PRIVATE platform_dsp platform_disc)
aurora_copy_runtime_dlls(xd_dsp_test)

add_executable(xd_dsp_run tools/xd_dsp_run.cpp)
target_link_libraries(xd_dsp_run PRIVATE platform_dsp platform_dsp_reference platform_disc)
aurora_copy_runtime_dlls(xd_dsp_run)

add_library(xd_datamods STATIC src/data/xd_data_mods.cpp)
target_include_directories(xd_datamods PUBLIC src/data)
target_link_libraries(xd_datamods PUBLIC engine_mods xd_port)

add_executable(xd_data_mods_tool tools/xd_data_mods.cpp)
set_target_properties(xd_data_mods_tool PROPERTIES OUTPUT_NAME xd_data_mods)
target_link_libraries(xd_data_mods_tool PRIVATE xd_datamods platform_os)
xd_use_decomp_headers(xd_data_mods_tool)
aurora_copy_runtime_dlls(xd_data_mods_tool)

add_library(xd_randomizer STATIC randomizer/xd_randomizer.cpp)
target_include_directories(xd_randomizer PUBLIC randomizer)
target_link_libraries(xd_randomizer PUBLIC engine_randomizer engine_mods)

add_executable(xd_randomize tools/xd_randomize.cpp)
target_link_libraries(xd_randomize PRIVATE xd_randomizer xd_datamods)
xd_use_decomp_headers(xd_randomize)
aurora_copy_runtime_dlls(xd_randomize)

add_executable(xd_data_dump tools/xd_data_dump.cpp)
target_link_libraries(xd_data_dump PRIVATE xd_port)
xd_use_decomp_headers(xd_data_dump)
aurora_copy_runtime_dlls(xd_data_dump)

add_library(xd_save STATIC save/xd_save.cpp save/xd_save_crypto.cpp save/xd_save_convert.cpp
  save/xd_save_write.cpp)
target_include_directories(xd_save PUBLIC save/include)

add_executable(xd_save_test save/tests/xd_save_test.cpp)
target_link_libraries(xd_save_test PRIVATE xd_save)

add_library(xd_save_names STATIC save/xd_save_names.cpp)
target_include_directories(xd_save_names PUBLIC save/include)
target_link_libraries(xd_save_names PUBLIC xd_save xd_port)
xd_use_decomp_headers(xd_save_names)

add_executable(xd_save_dump tools/xd_save_dump.cpp)
target_link_libraries(xd_save_dump PRIVATE xd_save xd_save_names engine_memcard)
aurora_copy_runtime_dlls(xd_save_dump)

add_executable(xd_save_convert tools/xd_save_convert.cpp)
target_link_libraries(xd_save_convert PRIVATE xd_save engine_memcard platform_os xd_card_root)
aurora_copy_runtime_dlls(xd_save_convert)

add_executable(xd_save_convert_test save/tests/xd_save_convert_test.cpp)
target_link_libraries(xd_save_convert_test PRIVATE xd_save engine_memcard)

add_library(xd_settings_list STATIC src/xd_settings.cpp)
target_link_libraries(xd_settings_list PUBLIC engine_settings engine_controls)

add_executable(xd_settings tools/xd_settings.cpp)
target_link_libraries(xd_settings PRIVATE xd_settings_list platform_os)
aurora_copy_runtime_dlls(xd_settings)

add_executable(xd_settings_test tools/xd_settings_test.cpp)
target_link_libraries(xd_settings_test PRIVATE xd_settings_list)

add_executable(xd_heap_model_test tools/xd_heap_model_test.cpp src/heap_model.cpp)

add_library(xd_extra_sources STATIC launcher/xd_extra_sources.cpp)
target_include_directories(xd_extra_sources PUBLIC launcher src ${PROJECT_SOURCE_DIR}/launcher/include)
target_link_libraries(xd_extra_sources PUBLIC engine_mods engine_settings)
add_executable(xd_extra_sources_test tools/xd_extra_sources_test.cpp)
target_link_libraries(xd_extra_sources_test PRIVATE xd_extra_sources xd_settings_list)

add_executable(xd_launcher WIN32 launcher/xd_launcher.cpp src/disc_verify.c)
target_include_directories(xd_launcher PRIVATE src)
target_link_libraries(xd_launcher PRIVATE xd_playthrough_files xd_randomizer xd_extra_sources) # (Save page: the randomizer's style)
target_link_libraries(xd_launcher PRIVATE launcher_ui xd_settings_list engine_disc_check platform_os xd_save
  xd_save_names engine_memcard ${AURORA_SDL3_TARGET})
target_compile_options(xd_launcher PRIVATE ${XD_FLAG_UTF8})
xd_link_windows_libs(xd_launcher PRIVATE ole32 shell32 uuid dxgi) # shortcuts (IShellLink), known folders; dxgi: graphics driver versions (shaders)
if (APPLE)
  set_target_properties(xd_launcher PROPERTIES LINK_OPTIONS "")
endif ()
if (NOT WIN32 AND NOT APPLE)
  set_target_properties(xd_launcher PROPERTIES BUILD_RPATH "\$ORIGIN")
  add_custom_command(TARGET xd_launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/resources/xd_icon.png"
      "$<TARGET_FILE_DIR:xd_launcher>/icon.png"
    VERBATIM)
endif ()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/updates.txt)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/updates.txt _xd_update_lines REGEX "^(feed|public_key)=")
set(_xd_feed "")
set(_xd_key "")
foreach (_line ${_xd_update_lines})
  if (_line MATCHES "^feed=(.*)$")
    string(STRIP "${CMAKE_MATCH_1}" _xd_feed)
  elseif (_line MATCHES "^public_key=(.*)$")
    string(STRIP "${CMAKE_MATCH_1}" _xd_key)
  endif ()
endforeach ()
if (DEFINED XD_UPDATE_FEED_URL)
  set(_xd_feed "${XD_UPDATE_FEED_URL}")
endif ()
if (DEFINED XD_UPDATE_PUBLIC_KEY)
  set(_xd_key "${XD_UPDATE_PUBLIC_KEY}")
endif ()
if (NOT _xd_key STREQUAL "" AND NOT _xd_key MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "games/xd/updates.txt: public_key must be 64 lowercase hex digits")
endif ()
string(LENGTH "${_xd_key}" _xd_key_length)
if (NOT _xd_key STREQUAL "" AND NOT _xd_key_length EQUAL 64)
  message(FATAL_ERROR "games/xd/updates.txt: public_key must be 64 lowercase hex digits")
endif ()
if (_xd_feed MATCHES "[\"\\\\]")
  message(FATAL_ERROR "games/xd/updates.txt: the feed address can't hold quotes or backslashes")
endif ()
if (_xd_feed STREQUAL "" OR _xd_key STREQUAL "")
  message(STATUS "xd: launcher without updater (games/xd/updates.txt is empty)")
else ()
  message(STATUS "xd: launcher updates from ${_xd_feed}")
endif ()
target_compile_definitions(xd_launcher PRIVATE XD_PORT_VERSION="${PROJECT_VERSION}" XD_UPDATE_FEED_URL="${_xd_feed}"
  XD_UPDATE_PUBLIC_KEY="${_xd_key}")
aurora_copy_runtime_dlls(xd_launcher)
launcher_copy_fonts(xd_launcher)

if (WIN32) # (Linux: no starter yet, docs/LINUX.md)
add_executable(xd_starter WIN32 launcher/starter.cpp)
set_target_properties(xd_starter PROPERTIES OUTPUT_NAME "Pokemon XD"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(xd_starter PRIVATE UNICODE _UNICODE)
endif ()

add_library(xd_gba STATIC gba/xd_gba_convert.cpp gba/xd_gba_trade.cpp)
target_include_directories(xd_gba PUBLIC gba/include)
target_link_libraries(xd_gba PUBLIC xd_save engine_gba_trade)

add_executable(xd_gba_test gba/tests/xd_gba_test.cpp)
target_link_libraries(xd_gba_test PRIVATE xd_gba)

add_library(xd_gba_disc STATIC gba/xd_gba_disc.cpp)
target_link_libraries(xd_gba_disc PUBLIC xd_gba xd_save_names xd_port)
xd_use_decomp_headers(xd_gba_disc)

add_executable(xd_gba_trade tools/xd_gba_trade.cpp)
target_link_libraries(xd_gba_trade PRIVATE xd_gba_disc engine_memcard)
aurora_copy_runtime_dlls(xd_gba_trade)

file(STRINGS "${XD_SNAPSHOT_DIR}/src/game/relglobal.c" _u32_tables REGEX "^u32 pokemon_data;")
if (_u32_tables)
  set(XD_RELGLOBAL_IS_U32 1)
else ()
  set(XD_RELGLOBAL_IS_U32 0)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(macho_symbols_test tools/macho_symbols_test.cpp)
endif ()

add_executable(xd_rel_link_test tools/xd_rel_link_test.cpp src/data/rel_link.cpp src/data/main_program_map.cpp)
if (MSVC)
  target_link_options(xd_rel_link_test PRIVATE /BASE:0x40000000 /DYNAMICBASE:NO)
elseif (APPLE) # (docs/MAC.md, never run)
  target_link_options(xd_rel_link_test PRIVATE ${XD_MAC_LOW_LINK_OPTIONS})
else () # Linux: a program that is not position independent is linked low (0x400000) by default (docs/LINUX.md)
  target_link_options(xd_rel_link_test PRIVATE -no-pie)
  set_target_properties(xd_rel_link_test PROPERTIES POSITION_INDEPENDENT_CODE OFF)
endif ()
target_link_libraries(xd_rel_link_test PRIVATE xd_port)
xd_use_decomp_headers(xd_rel_link_test)
target_compile_definitions(xd_rel_link_test PRIVATE XD_RELGLOBAL_IS_U32=0)
aurora_copy_runtime_dlls(xd_rel_link_test)

if (XD_BUILD_GAME)
set(XD_PROGRAM_LIBS xd_port engine_gs engine_disc_check engine_settings engine_controls xd_settings_list engine_mods xd_datamods xd_randomizer platform_compat platform_sdk platform_os platform_audio platform_input platform_video
  aurora::core aurora::gx aurora::os aurora::pad aurora::vi aurora::mtx aurora::card aurora::dvd aurora::si
  aurora::thp aurora::main)

set(XD_PROGRAM_PORT_SOURCES src/boot.c src/disc_tables.cpp src/disc_verify.c src/disc_choice.cpp src/game_new.cpp src/heap_check.cpp src/heap_model.cpp src/musyx_dsp_program.c
  src/renderdoc_capture.c src/screenshot.cpp src/bug_report.cpp src/settings_boot.cpp src/mods_boot.cpp src/state_dump.cpp src/timing_log.cpp src/cpu_cost_hooks.cpp src/fsys_unpack_cost.cpp src/dvd_open_cost.cpp src/bound_walk_cost.cpp src/perf_marks.cpp src/cpu_counters.cpp src/warp.cpp src/render_icon.cpp src/shader_prep.cpp src/pc_load.cpp src/dsp_coef.cpp
  src/data/rel_link.cpp src/data/main_program_map.cpp src/dev_menu.cpp src/task_stacks.cpp src/input_ticks.cpp
  src/particle_check.cpp src/settings_menu.cpp src/speedups.cpp src/move_split.cpp src/replay_pacing.cpp src/audio_categories.cpp src/button_icons.cpp src/settings_switches.cpp
  src/widescreen.cpp src/playthrough.cpp src/challenges.cpp src/level_caps.cpp src/battles_won.cpp src/walk_helper.cpp)
if (NOT WIN32)
  list(REMOVE_ITEM XD_PROGRAM_PORT_SOURCES src/bug_report.cpp src/screenshot.cpp src/state_dump.cpp src/render_icon.cpp)
  list(APPEND XD_PROGRAM_PORT_SOURCES src/not_on_linux.cpp)
  if (NOT APPLE)
    list(APPEND XD_PROGRAM_PORT_SOURCES src/state_dump_linux.cpp)
    list(APPEND XD_PROGRAM_PORT_SOURCES src/bug_report_linux.cpp)
  endif ()
endif ()
if (APPLE)
  list(APPEND XD_PROGRAM_PORT_SOURCES src/mac_low_reserve.c src/find_variable_mac.cpp)
  list(APPEND XD_PROGRAM_PORT_SOURCES ${PROJECT_SOURCE_DIR}/platform/os/low_arena.c
    ${PROJECT_SOURCE_DIR}/platform/os/low_malloc_mac.cpp ${PROJECT_SOURCE_DIR}/platform/os/low_malloc_selftest.cpp)
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/platform/os/low_arena.c ${PROJECT_SOURCE_DIR}/platform/os/low_malloc_mac.cpp
    ${PROJECT_SOURCE_DIR}/platform/os/low_malloc_selftest.cpp APPEND PROPERTY INCLUDE_DIRECTORIES
    ${PROJECT_SOURCE_DIR}/platform/os/include)
  set_source_files_properties(src/boot.c PROPERTIES COMPILE_OPTIONS "${XD_MAC_COMPAT_OPTION}")
endif ()
set_property(SOURCE src/dev_menu.cpp APPEND PROPERTY INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/platform/compat/include"
  "${PROJECT_SOURCE_DIR}/third_party/aurora/include" "${XD_SNAPSHOT_DIR}/include") # (as xd_use_decomp_headers)
execute_process(COMMAND git describe --always --dirty --abbrev=10
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE _xd_port_commit OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if (NOT _xd_port_commit AND DEFINED ENV{XD_PORT_COMMIT})
  set(_xd_port_commit "$ENV{XD_PORT_COMMIT}")
endif ()
if (_xd_port_commit)
  set_property(SOURCE src/boot.c APPEND PROPERTY COMPILE_DEFINITIONS "XD_PORT_COMMIT=\"${_xd_port_commit}\"")
endif ()
get_filename_component(_xd_snapshot_id "${XD_SNAPSHOT_DIR}" NAME)
set_property(SOURCE src/boot.c APPEND PROPERTY COMPILE_DEFINITIONS "XD_SNAPSHOT_ID=\"${_xd_snapshot_id}\"")
set_property(SOURCE src/boot.c APPEND PROPERTY COMPILE_DEFINITIONS "XD_PORT_VERSION=\"${PROJECT_VERSION}\"")
add_executable(xd ${XD_PROGRAM_PORT_SOURCES} $<TARGET_OBJECTS:xd_game> $<TARGET_OBJECTS:xd_musyx>)
target_compile_definitions(xd PRIVATE XD_RELGLOBAL_IS_U32=${XD_RELGLOBAL_IS_U32})
set_property(SOURCE src/warp.cpp src/render_icon.cpp src/settings_menu.cpp src/playthrough.cpp src/audio_categories.cpp src/button_icons.cpp src/settings_switches.cpp
  src/replay_pacing.cpp src/widescreen.cpp src/fsys_unpack_cost.cpp src/challenges.cpp src/walk_helper.cpp APPEND PROPERTY INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/platform/compat/include
  ${PROJECT_SOURCE_DIR}/third_party/aurora/include ${XD_SNAPSHOT_DIR}/include)
if (MSVC)
  set(XD_PROGRAM_LINK_OPTIONS /BASE:0x40000000 /DYNAMICBASE:NO /INCREMENTAL:NO)
elseif (APPLE) # macOS (docs/MAC.md, never run): the same address, and the reserved ranges of mac_low_reserve.c
  set(XD_PROGRAM_LINK_OPTIONS ${XD_MAC_LOW_LINK_OPTIONS}
    -Wl,-segaddr,__XDLOW,0x70000000 -Wl,-segaddr,__XDMEM1,0x80000000 -Wl,-segaddr,__XDMEMU,0xC0000000
    -Wl,-segaddr,__XDLC,0xE0000000 -Wl,-segaddr,__XDSTK,0x50000000)
else () # Linux (docs/LINUX.md, "Low memory"): not position independent, the image at the same fixed address
  set(XD_PROGRAM_LINK_OPTIONS -no-pie -Wl,-Ttext-segment=0x40000000)
endif ()
target_link_options(xd PRIVATE ${XD_PROGRAM_LINK_OPTIONS})
target_link_libraries(xd PRIVATE ${XD_PROGRAM_LIBS})
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set_target_properties(xd PROPERTIES BUILD_RPATH "\$ORIGIN")
endif ()
if (NOT MSVC)
  target_compile_options(xd PRIVATE ${XD_FLAGS_GAME_CODE} $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++11-narrowing>)
  target_link_options(xd PRIVATE -rdynamic) # function names in crash reports (platform/os/crash_report_linux.cpp)
  set(_xd_keep_inline_aliases ON)
  if (EXISTS "${XD_SNAPSHOT_DIR}/include/game/gcdata.h")
    file(STRINGS "${XD_SNAPSHOT_DIR}/include/game/gcdata.h" _xd_keep_inline_weak
      REGEX "define PC_KEEP_INLINE __attribute__\\(\\(used, weak\\)\\)")
    if (_xd_keep_inline_weak)
      set(_xd_keep_inline_aliases OFF)
    endif ()
  endif ()
  message(STATUS "xd: linker aliases for the kept inline constructors: ${_xd_keep_inline_aliases}")
  if (APPLE) # (Apple's linker spells it -alias <existing> <second name>; names carry one more underscore)
    if (_xd_keep_inline_aliases)
    target_link_options(xd PRIVATE -Wl,-alias,__ZN5GSvecC2Ev,__ZN5GSvecC1Ev -Wl,-alias,__ZN5GSmtxC2Ev,__ZN5GSmtxC1Ev
      -Wl,-alias,__ZN16GSBinWriteStreamD2Ev,__ZN16GSBinWriteStreamD1Ev)
    endif ()
  else ()
  if (_xd_keep_inline_aliases)
  target_link_options(xd PRIVATE -Wl,--defsym=_ZN5GSvecC1Ev=_ZN5GSvecC2Ev -Wl,--defsym=_ZN5GSmtxC1Ev=_ZN5GSmtxC2Ev
    -Wl,--defsym=_ZN16GSBinWriteStreamD1Ev=_ZN16GSBinWriteStreamD2Ev)
  endif ()
  target_link_options(xd PRIVATE -Wl,--wrap=aurora_imgui_add_texture)
  endif ()
endif ()
xd_link_windows_libs(xd PRIVATE windowscodecs ole32) # F12 screenshots, F10 bug reports (src/screenshot.cpp)
target_link_libraries(xd PRIVATE xd_playthrough_files)
set_property(SOURCE src/playthrough.cpp APPEND PROPERTY COMPILE_DEFINITIONS "XD_PORT_VERSION=\"${PROJECT_VERSION}\"")
target_link_libraries(xd PRIVATE xd_save_card) # the save in GameCube format on the card (src/save_card_filter.cpp)
set(_xd_pipelines_db "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/initial_pipeline_cache.db")
add_custom_command(OUTPUT "${_xd_pipelines_db}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/pipeline_list.py" build
    --list "${CMAKE_CURRENT_SOURCE_DIR}/shaders/pipelines.txt" --out "${_xd_pipelines_db}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/pipelines.txt" "${PROJECT_SOURCE_DIR}/tools/pipeline_list.py"
  COMMENT "Pipeline list -> initial_pipeline_cache.db" VERBATIM)
add_custom_target(xd_pipeline_list DEPENDS "${_xd_pipelines_db}")
add_dependencies(xd xd_pipeline_list)
set(_xd_dia "$ENV{VSINSTALLDIR}DIA SDK")
if(WIN32 AND EXISTS "${_xd_dia}/include/dia2.h" AND EXISTS "${_xd_dia}/bin/amd64/msdia140.dll")
  file(TO_CMAKE_PATH "${_xd_dia}/bin/amd64/msdia140.dll" _xd_dia_dll)
  set_property(SOURCE src/state_dump.cpp APPEND PROPERTY INCLUDE_DIRECTORIES "${_xd_dia}/include")
  set_property(SOURCE src/state_dump.cpp APPEND PROPERTY COMPILE_DEFINITIONS "XD_DIA_DLL=L\"${_xd_dia_dll}\"")
endif()
aurora_copy_runtime_dlls(xd)

option(XD_CALL_TRACE "Game functions log their calls (testing build for tools/call_diff.py)" OFF)
if (XD_CALL_TRACE)
  enable_language(ASM_MASM)
  target_compile_options(xd_game PRIVATE /Gh /Ob0)
  add_library(xd_call_trace_asm OBJECT src/call_trace_penter.asm)
  target_sources(xd PRIVATE src/call_trace.cpp $<TARGET_OBJECTS:xd_call_trace_asm>)
  target_sources(xd PRIVATE src/func_cost.cpp)
  include(sdk_cost_renames.cmake)
  set(XD_SDK_COST_RENAMES_LAST
    C_MTXConcat=PortSdk_PSMTXConcat C_MTXCopy=PortSdk_PSMTXCopy C_MTXIdentity=PortSdk_PSMTXIdentity
    C_MTXInvXpose=PortSdk_PSMTXInvXpose C_MTXInverse=PortSdk_PSMTXInverse C_MTXMultVec=PortSdk_PSMTXMultVec
    C_MTXMultVecSR=PortSdk_PSMTXMultVecSR C_MTXQuat=PortSdk_PSMTXQuat C_MTXRotAxisRad=PortSdk_PSMTXRotAxisRad
    C_MTXRotRad=PortSdk_PSMTXRotRad C_MTXScale=PortSdk_PSMTXScale C_MTXScaleApply=PortSdk_PSMTXScaleApply
    C_MTXTrans=PortSdk_PSMTXTrans C_MTXTransApply=PortSdk_PSMTXTransApply C_MTXTranspose=PortSdk_PSMTXTranspose
    C_QUATInverse=PortSdk_PSQUATInverse C_QUATMultiply=PortSdk_PSQUATMultiply C_VECAdd=PortSdk_PSVECAdd
    C_VECCrossProduct=PortSdk_PSVECCrossProduct C_VECDotProduct=PortSdk_PSVECDotProduct
    C_VECScale=PortSdk_PSVECScale C_VECSubtract=PortSdk_PSVECSubtract
    PortGX_CallDisplayList=PortXD_GXCallDisplayList PortGX_SetArrayUnsized=PortSdk_GXSetArrayUnsized)
  foreach (_src ${XD_GAME_SOURCES})
    if (NOT _src MATCHES "/bound\\.cpp$")
      set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_DEFINITIONS ${XD_SDK_COST_RENAMES}
        ${XD_SDK_COST_RENAMES_LAST})
    endif ()
  endforeach ()
  target_sources(xd PRIVATE src/sdk_cost.cpp)
endif ()

endif () # XD_BUILD_GAME

add_executable(xd_mods_check tools/xd_mods_check.cpp)
target_link_libraries(xd_mods_check PRIVATE engine_mods xd_datamods platform_disc platform_os aurora::core aurora::dvd aurora::gx aurora::vi)
aurora_copy_runtime_dlls(xd_mods_check)

add_executable(xd_disc_list tools/xd_disc_list.cpp)
target_link_libraries(xd_disc_list PRIVATE platform_disc engine_formats)
aurora_copy_runtime_dlls(xd_disc_list)

add_executable(xd_card_test tools/xd_card_test.cpp)
target_link_libraries(xd_card_test PRIVATE platform_sdk aurora::core aurora::card aurora::dvd aurora::os aurora::gx
  aurora::vi)
aurora_copy_runtime_dlls(xd_card_test)

add_library(xd_card_root STATIC src/xd_card_root.cpp)
target_include_directories(xd_card_root PUBLIC src)
target_link_libraries(xd_card_root PUBLIC engine_save_slots platform_os)
add_library(xd_playthrough_files STATIC src/xd_playthrough_files.cpp)
target_include_directories(xd_playthrough_files PUBLIC src)
target_link_libraries(xd_playthrough_files PUBLIC xd_save engine_memcard engine_playthrough platform_os xd_card_root)
add_library(xd_save_card STATIC src/save_card_filter.cpp)
target_link_libraries(xd_save_card PUBLIC xd_save aurora::card)

add_executable(xd_card_filter_test tools/xd_card_filter_test.cpp)
target_link_libraries(xd_card_filter_test PRIVATE xd_save_card engine_memcard aurora::core aurora::card aurora::os
  aurora::gx aurora::vi)
aurora_copy_runtime_dlls(xd_card_filter_test)

if (XD_BUILD_GAME)
add_library(xd_audio_test_musyx OBJECT EXCLUDE_FROM_ALL tools/xd_audio_test_musyx.c)
target_include_directories(xd_audio_test_musyx PRIVATE "${XD_SNAPSHOT_DIR}/extern/musyx/include"
  ${PROJECT_SOURCE_DIR}/platform/audio/musyx_sdk ${PROJECT_SOURCE_DIR}/platform/compat/include)
target_compile_definitions(xd_audio_test_musyx PRIVATE MUSY_TARGET=MUSY_TARGET_DOLPHIN MUSY_VERSION_MAJOR=2
  MUSY_VERSION_MINOR=0 MUSY_VERSION_PATCH=3 _USE_MATH_DEFINES)
set_property(TARGET xd_audio_test_musyx PROPERTY C_STANDARD 99)
target_compile_options(xd_audio_test_musyx PRIVATE ${XD_FLAG_UNDEF_DEBUG})  # as xd_musyx
if (NOT MSVC)
  target_include_directories(xd_audio_test_musyx BEFORE PRIVATE "${_xd_musyx_linux}")
  target_compile_options(xd_audio_test_musyx PRIVATE ${XD_MUSYX_LINUX_OPTIONS})
endif ()
add_executable(xd_audio_test EXCLUDE_FROM_ALL tools/xd_audio_test.cpp $<TARGET_OBJECTS:xd_audio_test_musyx>
  $<TARGET_OBJECTS:xd_musyx>)
target_link_options(xd_audio_test PRIVATE ${XD_PROGRAM_LINK_OPTIONS})
target_link_libraries(xd_audio_test PRIVATE platform_audio platform_sdk platform_disc platform_compat platform_os
  aurora::core aurora::os aurora::gx aurora::vi)
aurora_copy_runtime_dlls(xd_audio_test)

if (MSVC) # (Microsoft's librarian and linker switches; the survey is a Windows tool)
set(XD_SURVEY_RSP "${CMAKE_BINARY_DIR}/xd_link_survey_objs.rsp")
set(XD_SURVEY_LIB "${CMAKE_BINARY_DIR}/xd_link_survey_objs.lib")
if (NOT EXISTS "${XD_SURVEY_RSP}")
  file(TOUCH "${XD_SURVEY_RSP}")
endif ()
add_custom_command(OUTPUT "${XD_SURVEY_LIB}"
  COMMAND "${CMAKE_AR}" /nologo "/OUT:${XD_SURVEY_LIB}" "@${XD_SURVEY_RSP}"
  DEPENDS "${XD_SURVEY_RSP}"
  COMMENT "Bundling compiled game files for the link survey")
add_custom_target(xd_link_survey_objs DEPENDS "${XD_SURVEY_LIB}")
add_executable(xd_link_survey EXCLUDE_FROM_ALL ${XD_PROGRAM_PORT_SOURCES})
target_compile_definitions(xd_link_survey PRIVATE XD_RELGLOBAL_IS_U32=${XD_RELGLOBAL_IS_U32})
target_link_options(xd_link_survey PRIVATE ${XD_PROGRAM_LINK_OPTIONS})
add_dependencies(xd_link_survey xd_link_survey_objs)
target_link_libraries(xd_link_survey PRIVATE ${XD_PROGRAM_LIBS})
target_link_options(xd_link_survey PRIVATE "/WHOLEARCHIVE:${XD_SURVEY_LIB}" "${XD_SURVEY_LIB}")
set_property(TARGET xd_link_survey APPEND PROPERTY LINK_DEPENDS "${XD_SURVEY_LIB}")
endif () # MSVC

endif () # XD_BUILD_GAME

get_property(_xd_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach (_t ${_xd_targets})
  if (NOT WIN32) # (Windows resource files; a Linux program has no icon inside it)
    break()
  endif ()
  get_target_property(_type ${_t} TYPE)
  if (_t STREQUAL "xd_launcher") # (the same, plus the other icon styles it offers: xd_launcher.rc)
    target_sources(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/xd_launcher.rc)
  elseif (_type STREQUAL "EXECUTABLE")
    target_sources(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/xd.rc)
  endif ()
endforeach ()
