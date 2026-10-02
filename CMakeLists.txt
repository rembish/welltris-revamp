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

# Replay tool used by re/emu/difftest.py
add_executable(wt_replay tests/replay.c)
target_link_libraries(wt_replay PRIVATE wtcore)

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
endif()
