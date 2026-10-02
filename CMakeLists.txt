cmake_minimum_required(VERSION 3.16)
project(welltris C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(wtcore STATIC core/wt_core.c core/wt_tables.c)
target_include_directories(wtcore PUBLIC core)

# Embed the font so the binary has no runtime asset paths.
set(FONT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/assets/Exo2.ttf)
set(FONT_C ${CMAKE_CURRENT_BINARY_DIR}/font_data.c)
add_custom_command(
  OUTPUT ${FONT_C}
  COMMAND ${CMAKE_COMMAND} -DIN=${FONT_SRC} -DOUT=${FONT_C} -DNAME=font_ttf -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${FONT_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake)

# The original artwork is never part of the repository. re/tools/assets.py builds an upscaled set
# from your own copy into assets-local/; WT_LOCAL_ASSETS embeds it into a private build.
option(WT_LOCAL_ASSETS "Embed the art built by re/tools/assets.py from your own original files" OFF)
set(ART_C ${CMAKE_CURRENT_BINARY_DIR}/art_data.c)
set(ART_DIR "")
set(ART_DEPS "")
if(WT_LOCAL_ASSETS)
  set(ART_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets-local/hd)
  file(GLOB ART_DEPS ${ART_DIR}/*)
endif()
add_custom_command(
  OUTPUT ${ART_C}
  COMMAND ${CMAKE_COMMAND} -DDIR=${ART_DIR} -DOUT=${ART_C} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/art_index.cmake
  DEPENDS ${ART_DEPS} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/art_index.cmake)

option(WT_VENDOR_SDL "Build SDL2 from source and link it statically (for redistributable binaries)" OFF)

set(WT_GUI_FLAG "")
if(WIN32)
  set(WT_GUI_FLAG WIN32)
elseif(APPLE)
  set(WT_GUI_FLAG MACOSX_BUNDLE)
endif()

add_executable(welltris ${WT_GUI_FLAG} src/main.c src/gfx.c src/font.c src/view.c src/art.c src/audio.c src/store.c
  src/hof.c src/touch.c third_party/stb_truetype_impl.c third_party/stb_image_impl.c ${FONT_C} ${ART_C})
target_link_libraries(welltris PRIVATE wtcore)

if(EMSCRIPTEN)
  set(SDL_FLAGS "-sUSE_SDL=2")
  target_compile_options(welltris PRIVATE ${SDL_FLAGS})
  target_link_options(welltris PRIVATE ${SDL_FLAGS} -sALLOW_MEMORY_GROWTH=1
    --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
  set_target_properties(welltris PROPERTIES SUFFIX ".html")
else()
  if(WT_VENDOR_SDL)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz)
    FetchContent_MakeAvailable(SDL2)
    target_link_libraries(welltris PRIVATE SDL2::SDL2-static)
  else()
    find_package(SDL2 REQUIRED)
    target_link_libraries(welltris PRIVATE SDL2::SDL2)
  endif()
  if(TARGET SDL2::SDL2main)
    target_link_libraries(welltris PRIVATE SDL2::SDL2main)
  endif()
  if(NOT WIN32)
    target_link_libraries(welltris PRIVATE m)
  endif()
  if(MINGW AND WT_VENDOR_SDL)
    target_link_options(welltris PRIVATE -static)   # no libgcc/winpthread DLLs to ship
  endif()
  if(APPLE)
    set_target_properties(welltris PROPERTIES
      MACOSX_BUNDLE_BUNDLE_NAME "Welltris"
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.welltris.revamp"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
  endif()
endif()

# Replay and bot tools used by re/emu/difftest.py
add_executable(wt_replay tests/replay.c)
target_link_libraries(wt_replay PRIVATE wtcore)
add_executable(wt_botgen tests/botgen.c)
target_link_libraries(wt_botgen PRIVATE wtcore)

option(WT_WERROR "Treat warnings as errors" OFF)
if(NOT MSVC)
  set(WT_WARN -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wcast-qual -Wstrict-prototypes
      -Wmissing-prototypes -Wnull-dereference -Wvla -Wdouble-promotion)
  if(WT_WERROR)
    list(APPEND WT_WARN -Werror)
  endif()
  # The core reproduces 16-bit arithmetic, so every narrowing must be explicit there.
  set(WT_WARN_STRICT ${WT_WARN} -Wconversion -Wsign-conversion)
  target_compile_options(wtcore PRIVATE ${WT_WARN_STRICT})
  target_compile_options(wt_replay PRIVATE ${WT_WARN_STRICT})
  target_compile_options(wt_botgen PRIVATE ${WT_WARN_STRICT})
  target_compile_options(welltris PRIVATE ${WT_WARN})
  # third-party code and generated data
  set_source_files_properties(third_party/stb_truetype_impl.c third_party/stb_image_impl.c ${ART_C} ${FONT_C} PROPERTIES COMPILE_OPTIONS "-w")
endif()
