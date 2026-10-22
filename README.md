# sim_iso8583 (skeleton)

sim_iso8583 is an ISO 8583 simulator: a TCP server in C99 that answers ISO 8583 messages (0800 and 0200), and a Python test client.
The server is a skeleton: the TODO comments in `server/src/*.c` describe what to write.

Read `docs/ARCHITECTURE.md` first. It explains the communication, the message format and the business rules.

## Layout

    sim_iso8583/      project root
    server/include/   headers
    server/src/       main.c, connection.c, framing.c, sim_iso8583.c, handler.c
    tools/            throwaway_client.py (test client), check_llvm.sh (toolchain check)
    docs/             ARCHITECTURE.md
    Makefile          command-line build (GNU make)
    CMakeLists.txt    same sources, for Visual Studio with a WSL target
    CMakePresets.json Linux Debug and Linux Release configurations

## Build and run (Linux, or WSL on Windows)

    make                 # debug build -> build/debug/sim_iso8583d
    make release         # optimized build
    make run             # build and start on port 8583
    make clean
    make CC=gcc          # use gcc instead of clang (default)
    make SAN=1           # AddressSanitizer + UBSan

With CMake (what Visual Studio uses):

    cmake --preset linux-debug               # clang; also: linux-release, linux-asan, linux-gcc-debug
    cmake --build out/build/linux-debug      # -> out/build/linux-debug/sim_iso8583d

Visual Studio on Windows: see docs/ARCHITECTURE.md section 12.

## Test

    python3 tools/throwaway_client.py            # all scenarios
    python3 tools/throwaway_client.py --only luhn
    python3 tools/throwaway_client.py --list
    python3 tools/throwaway_client.py --dump     # show the request bytes, no server needed
