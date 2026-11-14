# GoatTracker Ultimate plugin (VST3, AAX, AU) - the JUCE shell
#
# The tracker itself is the core library gtcore (plugin/core), built first
# (on Windows with llvm-mingw, like the standalone program). This project
# builds the plugin around it and puts the library and the fonts into the
# plugin bundles. See plugin/README.md.
#
#   cmake -S plugin -B build-plugin -DJUCE_DIR=<JUCE> -DGTCORE_LIBRARY=<gtcore.dll / gtcore.dylib>
#         [-DAAX_SDK_PATH=<AAX SDK>]

cmake_minimum_required(VERSION 3.22)
project(GoatTrackerUltimatePlugin VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

set(JUCE_DIR "" CACHE PATH "JUCE 8 source folder")
set(GTCORE_LIBRARY "" CACHE FILEPATH "The built core library (gtcore.dll / gtcore.dylib)")
set(AAX_SDK_PATH "" CACHE PATH "AAX SDK folder (optional: builds the AAX plugin for Pro Tools)")

if(NOT JUCE_DIR)
  message(FATAL_ERROR "Set JUCE_DIR to the JUCE 8 source folder")
endif()
if(NOT EXISTS "${GTCORE_LIBRARY}")
  message(FATAL_ERROR "Set GTCORE_LIBRARY to the built core library (plugin/core)")
endif()

add_subdirectory(${JUCE_DIR} JUCE)

set(GT_FORMATS VST3)
if(APPLE)
  list(APPEND GT_FORMATS AU)
endif()
if(AAX_SDK_PATH)
  juce_set_aax_sdk_path(${AAX_SDK_PATH})
  list(APPEND GT_FORMATS AAX)
endif()

juce_add_plugin(GoatTrackerUltimate
  COMPANY_NAME "GoatTracker Ultimate"
  COMPANY_WEBSITE "https://github.com/brettcook99/silver-fork-catalina"
  PRODUCT_NAME "GoatTracker Ultimate"
  BUNDLE_ID "net.sourceforge.goattracker2.silverfork.plugin"
  DESCRIPTION "SID music tracker"
  PLUGIN_MANUFACTURER_CODE Gtul
  PLUGIN_CODE Gtk2
  FORMATS ${GT_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  AAX_CATEGORY AAX_ePlugInCategory_SWGenerators
  AU_SANDBOX_SAFE FALSE
  HARDENED_RUNTIME_ENABLED FALSE)

target_sources(GoatTrackerUltimate PRIVATE
  shell/PluginProcessor.cpp
  shell/PluginEditor.cpp
  shell/CoreLoader.cpp
  shell/KeyMap.cpp)

target_include_directories(GoatTrackerUltimate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin)

target_compile_definitions(GoatTrackerUltimate PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0
  JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(GoatTrackerUltimate
  PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# A small host for testing the plugin without a DAW (plugin/tests)
option(GT_BUILD_TESTHOST "Build the test host gttesthost" OFF)
if(GT_BUILD_TESTHOST)
  juce_add_gui_app(gttesthost PRODUCT_NAME "gttesthost")
  target_sources(gttesthost PRIVATE tests/TestHost.cpp)
  target_compile_definitions(gttesthost PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="gttesthost" JUCE_APPLICATION_VERSION_STRING="1.0")
  target_link_libraries(gttesthost PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags)
endif()

# The core library and the fonts go into each plugin bundle:
#   Windows  <bundle>/Contents/x86_64-win/gtcore.dll, <bundle>/Contents/Resources/fonts
#   macOS    <bundle>/Contents/Frameworks/gtcore.dylib, <bundle>/Contents/Resources/fonts
set(GT_FONTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../fonts)
foreach(fmt ${GT_FORMATS})
  set(tgt GoatTrackerUltimate_${fmt})
  if(WIN32)
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GTCORE_LIBRARY}" "$<TARGET_FILE_DIR:${tgt}>/gtcore.dll"
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${tgt}>/../Resources/fonts"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${GT_FONTS_DIR}" "$<TARGET_FILE_DIR:${tgt}>/../Resources/fonts"
      VERBATIM)
  else()
    add_custom_command(TARGET ${tgt} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${tgt}>/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GTCORE_LIBRARY}" "$<TARGET_BUNDLE_CONTENT_DIR:${tgt}>/Frameworks/gtcore.dylib"
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${tgt}>/Resources/fonts"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${GT_FONTS_DIR}" "$<TARGET_BUNDLE_CONTENT_DIR:${tgt}>/Resources/fonts"
      VERBATIM)
  endif()
endforeach()
