# GoatTracker core library for the plugin (gtcore.dll / gtcore.dylib)
#
# All of the tracker (the same sources as the standalone program) built with
# GT_PLUGIN, plus the plugin layer in src/plugin/. SDL2 is linked statically,
# built with the offscreen video driver and without audio (see
# plugin/cmake/sdl2-static.cmake / plugin/build_win.sh).
#
#   cmake -S plugin/core -B build-core -DSDL2_DIR=<sdl2 install>/lib/cmake/SDL2

cmake_minimum_required(VERSION 3.20)
project(gtcore C CXX)

set(GT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

find_package(SDL2 CONFIG REQUIRED)

set(GT_CORE_SOURCES
  goattrk2.c greloc.c gfileio.c gmidifile.c gsngwrite.c gsidimport.c cpu6502.c gfermatax.c gpatch.c gretro.c ghelp.c
  goatdata.c gsong.c gorder.c gpattern.c ginstr.c gtable.c gplay.c gdisplay.c gfile.c gsound.c gconsole.c gsid.cpp
  gundo.c gmidi.c gcommand.c)
set(GT_GUI_SOURCES
  gui/gui_main.cpp gui/gui_widgets.cpp gui/gui_transport.cpp gui/gui_pattern.cpp gui/gui_tracks.cpp gui/gui_instr.cpp
  gui/gui_tables.cpp gui/gui_song.cpp gui/gui_dialogs.cpp gui/gui_help.cpp gui/gui_retro.cpp gui/gui_c64screen.cpp
  gui/gui_designer.cpp gui/gui_image.cpp
  imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp imgui/imgui_impl_sdl2.cpp
  imgui/imgui_impl_sdlrenderer2.cpp)
set(GT_RESID_SOURCES
  resid/envelope.cpp resid/extfilt.cpp resid/filter.cpp resid/pot.cpp resid/sid.cpp resid/wave.cpp resid/wave6581__ST.cpp
  resid/wave6581_P_T.cpp resid/wave6581_PS_.cpp resid/wave6581_PST.cpp resid/wave8580__ST.cpp resid/wave8580_P_T.cpp
  resid/wave8580_PS_.cpp resid/wave8580_PST.cpp resid/voice.cpp
  resid-fp/envelopefp.cpp resid-fp/extfiltfp.cpp resid-fp/filterfp.cpp resid-fp/potfp.cpp resid-fp/sidfp.cpp
  resid-fp/wavefp.cpp resid-fp/voicefp.cpp)
set(GT_ASM_SOURCES
  asm/asmtab.c asm/chnkpool.c asm/expr.c asm/lexyy.c asm/log.c asm/membuf.c asm/membufio.c asm/namedbuf.c asm/parse.c
  asm/pc.c asm/vec.c)
set(GT_BME_SOURCES bme/bme_snd.c bme/bme_win.c bme/bme_io.c bme/bme_end.c bme/bme.c)
set(GT_PLUGIN_SOURCES plugin/gtplugin.c plugin/gtplugin_audio.c)

set(ALL_SOURCES)
foreach(f ${GT_CORE_SOURCES} ${GT_GUI_SOURCES} ${GT_RESID_SOURCES} ${GT_ASM_SOURCES} ${GT_BME_SOURCES} ${GT_PLUGIN_SOURCES})
  list(APPEND ALL_SOURCES ${GT_SRC}/${f})
endforeach()

add_library(gtcore SHARED ${ALL_SOURCES})
set_target_properties(gtcore PROPERTIES PREFIX "" OUTPUT_NAME gtcore
  C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
target_include_directories(gtcore PRIVATE ${GT_SRC} ${GT_SRC}/bme ${GT_SRC}/asm)
target_compile_definitions(gtcore PRIVATE GT_PLUGIN SDL_MAIN_HANDLED)
target_link_libraries(gtcore PRIVATE SDL2::SDL2-static)

# The same language settings as the standalone build (makefile.common,
# build_catalina.sh): reSID as old C++, the user interface as C++11
foreach(f ${GT_RESID_SOURCES} gsid.cpp)
  set_source_files_properties(${GT_SRC}/${f} PROPERTIES COMPILE_OPTIONS "-std=gnu++98")
endforeach()
foreach(f ${GT_GUI_SOURCES})
  set_source_files_properties(${GT_SRC}/${f} PROPERTIES COMPILE_OPTIONS "-std=c++11")
endforeach()
# The assembler's exit() calls must not end the host (plugin/gtexit.h)
foreach(f ${GT_ASM_SOURCES} greloc.c)
  set_source_files_properties(${GT_SRC}/${f} PROPERTIES COMPILE_OPTIONS "-include;${GT_SRC}/plugin/gtexit.h")
endforeach()

target_compile_options(gtcore PRIVATE -O2 -Wno-everything)

if(WIN32)
  # Everything inside the DLL: a private copy of it is loaded for every
  # plugin instance, from a temporary folder
  target_link_options(gtcore PRIVATE -static -Wl,--no-insert-timestamp)
  target_link_libraries(gtcore PRIVATE winmm imm32 setupapi version ole32 oleaut32 gdi32 user32 shell32 advapi32 uuid)
elseif(APPLE)
  set_target_properties(gtcore PROPERTIES SUFFIX ".dylib")
  target_link_libraries(gtcore PRIVATE "-framework CoreFoundation" "-framework CoreMIDI")
endif()

# A test program that renders without a host (plugin/tests/)
if(NOT CMAKE_CROSSCOMPILING)
  add_executable(gtcore_render ${CMAKE_CURRENT_SOURCE_DIR}/../tests/gtcore_render.c)
  target_include_directories(gtcore_render PRIVATE ${GT_SRC}/plugin)
  if(WIN32)
    target_link_options(gtcore_render PRIVATE -static)
  else()
    target_link_libraries(gtcore_render PRIVATE dl)
  endif()
endif()
