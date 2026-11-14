# SDL2 settings for the plugin's core library (cmake -C sdl2-plugin.cmake):
# a static library with only what the plugin uses. The window is drawn into
# memory (offscreen video driver, software renderer), the host plays the
# audio, and on macOS nothing defines Objective-C classes (every plugin
# instance loads its own copy of the core library, and the same class in two
# copies would clash).

set(SDL_SHARED OFF CACHE BOOL "")
set(SDL_STATIC ON CACHE BOOL "")
set(SDL_STATIC_PIC ON CACHE BOOL "")
set(SDL_TEST OFF CACHE BOOL "")
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "")
set(SDL_AUDIO OFF CACHE BOOL "")
set(SDL_JOYSTICK OFF CACHE BOOL "")
set(SDL_HAPTIC OFF CACHE BOOL "")
set(SDL_SENSOR OFF CACHE BOOL "")
set(SDL_HIDAPI OFF CACHE BOOL "")
set(SDL_POWER OFF CACHE BOOL "")
set(SDL_OPENGL OFF CACHE BOOL "")
set(SDL_OPENGLES OFF CACHE BOOL "")
set(SDL_VULKAN OFF CACHE BOOL "")
set(SDL_METAL OFF CACHE BOOL "")
set(SDL_RENDER_METAL OFF CACHE BOOL "")
set(SDL_DIRECTX OFF CACHE BOOL "")
set(SDL_RENDER_D3D OFF CACHE BOOL "")
set(SDL_COCOA OFF CACHE BOOL "")
set(SDL_X11 OFF CACHE BOOL "")
set(SDL_WAYLAND OFF CACHE BOOL "")
set(SDL_OFFSCREEN ON CACHE BOOL "")
set(SDL_DUMMYVIDEO ON CACHE BOOL "")
