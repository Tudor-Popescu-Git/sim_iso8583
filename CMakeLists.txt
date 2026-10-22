# CMake project for sim_iso8583 (ISO 8583 simulator).
#
# This file exists so that Visual Studio can build the server with a Linux
# toolchain (WSL). The Makefile builds the same sources and is the main build
# for the command line. Keep the two source lists identical.

cmake_minimum_required(VERSION 3.16)

project(sim_iso8583 VERSION 0.1 LANGUAGES C)

# The server uses fork(), sigaction() and <sys/socket.h>. Microsoft's compiler
# (MSVC) cannot build them. Stop early with a clear message instead of
# producing hundreds of compiler errors.
if(NOT UNIX)
  message(FATAL_ERROR
    "sim_iso8583 needs a Linux toolchain (fork and POSIX sockets).\n"
    "In Visual Studio: choose the WSL target system in the toolbar, choose the "
    "'Linux Debug' configuration, then Project > Delete Cache and Reconfigure.")
endif()

# Strict C99 (-std=c99, not -std=gnu99), same as the Makefile.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# When no build type is given on the command line, build the debug version.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Source files are listed one by one on purpose: CMake does not notice new
# files found by a wildcard until it is run again. Add new files here.
add_executable(sim_iso8583d
  server/src/main.c
  server/src/connection.c
  server/src/framing.c
  server/src/sim_iso8583.c
  server/src/handler.c
)

target_include_directories(sim_iso8583d PRIVATE server/include)

# _POSIX_C_SOURCE: with -std=c99 the POSIX functions are hidden unless requested.
target_compile_definitions(sim_iso8583d PRIVATE
  _POSIX_C_SOURCE=200809L
  $<$<CONFIG:Debug>:DEBUG>
  $<$<CONFIG:Release>:NDEBUG>
)

target_compile_options(sim_iso8583d PRIVATE -Wall -Wextra -pedantic)

# The compiler comes from CMakePresets.json: clang by default, gcc in the
# "gcc" preset. Both must build without warnings.
if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  message(WARNING "Compiler ${CMAKE_C_COMPILER_ID} is not tested with this project.")
endif()
message(STATUS "C compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")

# Optional: AddressSanitizer and UndefinedBehaviorSanitizer (preset "linux-asan",
# or add -DSIM_SANITIZE=ON). They report buffer over-reads and other memory
# errors at run time. The program runs slower; use it for testing only.
option(SIM_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(SIM_SANITIZE)
  target_compile_options(sim_iso8583d PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(sim_iso8583d PRIVATE -fsanitize=address,undefined)
endif()
