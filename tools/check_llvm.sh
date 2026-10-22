#!/usr/bin/env bash
# check_llvm.sh - check that the LLVM/clang toolchain is usable for sim_iso8583.
#
# Run it inside Ubuntu (WSL), not in PowerShell, not in Git Bash:
#     bash tools/check_llvm.sh            (from the project folder; the project is found automatically)
#     bash check_llvm.sh /path/to/project (when the script is stored somewhere else)
#
# The script changes nothing on the system. It does not install anything.
# It writes only into a temporary directory, which is deleted when the script ends.
# It does not start the server and does not open any network connection.
#
# Result levels:
#   PASS  works
#   WARN  optional part is missing or broken; the project can still be built
#   FAIL  required part is missing or broken; fix this before switching to clang

set -u

PASS=0; WARN=0; FAIL=0
ok()   { printf '  [PASS] %s\n' "$1"; PASS=$((PASS+1)); }
warn() { printf '  [WARN] %s\n' "$1"; WARN=$((WARN+1)); }
bad()  { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL+1)); }
note() { printf '         %s\n' "$1"; }
head_() { printf '\n== %s\n' "$1"; }

TMP=$(mktemp -d 2>/dev/null) || { echo "Cannot create a temporary directory."; exit 2; }
trap 'rm -rf "$TMP"' EXIT

if [ -n "${1:-}" ]; then ROOT=$1
elif [ -f "$(dirname "$0")/../CMakeLists.txt" ]; then ROOT=$(cd "$(dirname "$0")/.." && pwd)
else ROOT=.; fi
TO=""; command -v timeout >/dev/null 2>&1 && TO="timeout 60"

echo "sim_iso8583: LLVM / clang check"
echo "Temporary files go to $TMP and are deleted at the end."

# ---------------------------------------------------------------------------
head_ "1. Environment"
if [ "$(uname -s)" != "Linux" ]; then
  bad "This is not Linux ($(uname -s)). Run the script inside Ubuntu (WSL)."
  note "In PowerShell:  wsl   (opens Ubuntu), then run the script again."
  echo; echo "Result: FAIL (wrong environment)"; exit 1
fi
ok "Linux, $(uname -m), $(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-unknown distribution}")"
if grep -qi microsoft /proc/version 2>/dev/null; then
  ok "Running inside WSL"
else
  note "Not WSL (a normal Linux system). That is fine."
fi
[ "$(uname -m)" = "x86_64" ] || warn "CPU is not x86_64; this check was written for x86_64."

# ---------------------------------------------------------------------------
head_ "2. clang compiler"
CLANG_MAJOR=0
if command -v clang >/dev/null 2>&1; then
  ok "clang found at $(command -v clang)"
  VERLINE=$(clang --version 2>/dev/null | head -1)
  note "$VERLINE"
  CLANG_MAJOR=$(printf '%s' "$VERLINE" | sed -n 's/.*version \([0-9][0-9]*\)\..*/\1/p')
  CLANG_MAJOR=${CLANG_MAJOR:-0}
  if [ "$CLANG_MAJOR" -ge 14 ] 2>/dev/null; then
    ok "version $CLANG_MAJOR is recent enough (14 or newer)"
  else
    warn "could not read the version or it is older than 14"
  fi
  TARGET=$(clang -dumpmachine 2>/dev/null)
  case "$TARGET" in
    *linux*) ok "target is $TARGET" ;;
    *)       bad "target is '$TARGET', expected a Linux target" ;;
  esac
else
  bad "clang is not installed (or not in PATH)"
  note "Install:  sudo apt update && sudo apt install clang"
fi

# ---------------------------------------------------------------------------
head_ "3. Other tools"
need() {  # need <command> <install hint>   (required)
  if command -v "$1" >/dev/null 2>&1; then ok "$1: $("$1" --version 2>&1 | head -1)"
  else bad "$1 not found"; note "Install: $2"; fi
}
want() {  # want <command> <what it is for> <install hint>   (optional)
  if command -v "$1" >/dev/null 2>&1; then ok "$1 found"
  else warn "$1 not found (used for: $2)"; note "Install: $3"; fi
}
need cmake "sudo apt install cmake"
need ninja "sudo apt install ninja-build"
want make    "the Makefile build"                 "sudo apt install build-essential"
want ld.lld  "linking with LLVM's linker (lld)"    "sudo apt install lld"
want lldb    "debugging with LLVM's debugger"      "sudo apt install lldb"
want gdb     "core-dump analysis on Day 3"         "sudo apt install gdb"
want python3 "the test client"                     "sudo apt install python3"
SYMB=""
for c in llvm-symbolizer "llvm-symbolizer-$CLANG_MAJOR"; do
  command -v "$c" >/dev/null 2>&1 && SYMB=$c && break
done
if [ -n "$SYMB" ]; then ok "$SYMB found (readable sanitizer reports)"
else warn "llvm-symbolizer not found (sanitizer reports will show addresses, not line numbers)"
     note "Install: sudo apt install llvm"; fi

# Everything below needs a working clang.
if ! command -v clang >/dev/null 2>&1; then
  echo; echo "Result: FAIL - clang is missing, the compile tests were skipped."
  echo "PASS=$PASS WARN=$WARN FAIL=$FAIL"; exit 1
fi

# ---------------------------------------------------------------------------
head_ "4. Compile and run a POSIX test with the project's exact flags"
cat > "$TMP/posix.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(void)
{
    struct sigaction sa;
    struct sockaddr_in addr;
    int fd, status;
    pid_t pid;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) != 0) return 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 2;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8583);
    if (ntohs(addr.sin_port) != 8583) return 3;
    close(fd);

    pid = fork();
    if (pid < 0) return 4;
    if (pid == 0) _exit(7);
    if (waitpid(pid, &status, 0) != pid) return 5;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 7) return 6;
    puts("posix test ok");
    return 0;
}
EOF
FLAGS="-std=c99 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200809L"
if clang $FLAGS -Werror -g -O0 "$TMP/posix.c" -o "$TMP/posix" 2> "$TMP/err.txt"; then
  ok "compiled with: clang $FLAGS -Werror (no warnings)"
else
  bad "compile failed"; sed 's/^/         /' "$TMP/err.txt" | head -15
fi
if [ -x "$TMP/posix" ]; then
  OUT=$($TO "$TMP/posix" 2>&1); RC=$?
  if [ $RC -eq 0 ] && [ "$OUT" = "posix test ok" ]; then
    ok "ran: socket(), sigaction(), fork() and waitpid() work"
  else
    bad "program returned $RC (1 sigaction, 2 socket, 4 fork, 5 or 6 waitpid)"; note "$OUT"
  fi
fi
cat > "$TMP/endian.c" <<'EOF'
#include <stdio.h>
int main(void){ unsigned short v = 0x0102; unsigned char *p = (unsigned char *)&v;
  puts(p[0] == 0x02 ? "little-endian" : "big-endian");
  printf("sizeof(long)=%u sizeof(void*)=%u\n", (unsigned)sizeof(long), (unsigned)sizeof(void *)); return 0; }
EOF
if clang "$TMP/endian.c" -o "$TMP/endian" 2>/dev/null; then
  "$TMP/endian" | while read -r l; do note "this machine: $l"; done
fi

# ---------------------------------------------------------------------------
head_ "5. LLVM linker (lld)"
if command -v ld.lld >/dev/null 2>&1; then
  if clang -fuse-ld=lld "$TMP/posix.c" -D_POSIX_C_SOURCE=200809L -o "$TMP/posix_lld" 2> "$TMP/err.txt" \
     && [ "$($TO "$TMP/posix_lld" 2>&1)" = "posix test ok" ]; then
    ok "linking with -fuse-ld=lld works and the program runs"
  else
    warn "linking with lld failed"; sed 's/^/         /' "$TMP/err.txt" | head -5
  fi
else
  note "skipped (ld.lld not installed)"
fi

# ---------------------------------------------------------------------------
head_ "6. Sanitizers (find memory errors; useful for the parser on Day 3)"
cat > "$TMP/overflow.c" <<'EOF'
#include <stdlib.h>
int main(void){ char *p = malloc(4); p[4] = 1; free(p); return 0; }
EOF
cat > "$TMP/ubsan.c" <<'EOF'
#include <limits.h>
#include <stdio.h>
int main(int argc, char **argv){ (void)argv; int x = INT_MAX - 1 + argc; x = x + 1; printf("%d\n", x); return 0; }
EOF
export ASAN_OPTIONS=detect_leaks=0
if clang -fsanitize=address -g -O0 "$TMP/overflow.c" -o "$TMP/overflow" 2> "$TMP/err.txt"; then
  OUT=$($TO "$TMP/overflow" 2>&1); RC=$?
  if [ $RC -ne 0 ] && printf '%s' "$OUT" | grep -q "heap-buffer-overflow"; then
    ok "AddressSanitizer detects a deliberate heap overflow"
    if printf '%s' "$OUT" | grep -q "overflow.c:2"; then ok "report shows the source line (symbolizer works)"
    else warn "report has no source line; install llvm for llvm-symbolizer"; fi
  else
    warn "AddressSanitizer compiled but did not report the overflow (return code $RC)"
    printf '%s\n' "$OUT" | head -4 | sed 's/^/         /'
  fi
else
  warn "AddressSanitizer does not link"
  grep -m2 -E "cannot find|error" "$TMP/err.txt" | sed 's/^/         /'
  note "On Ubuntu the runtime is a separate package. Install:  sudo apt install libclang-rt-${CLANG_MAJOR}-dev"
fi
if clang -fsanitize=undefined -g -O0 "$TMP/ubsan.c" -o "$TMP/ubsan" 2> "$TMP/err.txt"; then
  OUT=$($TO "$TMP/ubsan" 2>&1)
  if printf '%s' "$OUT" | grep -q "runtime error"; then ok "UndefinedBehaviorSanitizer detects signed overflow"
  else warn "UndefinedBehaviorSanitizer compiled but reported nothing"; fi
else
  warn "UndefinedBehaviorSanitizer does not link"
  note "Install:  sudo apt install libclang-rt-${CLANG_MAJOR}-dev"
fi

# ---------------------------------------------------------------------------
head_ "7. Debugger (lldb)"
if command -v lldb >/dev/null 2>&1; then
  OUT=$($TO lldb --batch -o "breakpoint set -n main" -o "run" -o "bt" -o "kill" "$TMP/posix" 2>&1 || true)
  if printf '%s' "$OUT" | grep -q "stop reason = breakpoint"; then
    ok "lldb stops at a breakpoint in main"
  else
    warn "lldb could not stop at a breakpoint (often a WSL permission problem; gdb is the alternative)"
    printf '%s\n' "$OUT" | tail -3 | sed 's/^/         /'
  fi
else
  note "skipped (lldb not installed)"
fi

# ---------------------------------------------------------------------------
head_ "8. The project itself, built with clang (from a copy; your files are not touched)"
if [ -f "$ROOT/CMakeLists.txt" ] && [ -f "$ROOT/Makefile" ] && [ -d "$ROOT/server/src" ]; then
  mkdir -p "$TMP/src"
  ( cd "$ROOT" && tar --exclude=./out --exclude=./.vs --exclude=./build --exclude=./.git -cf - . ) | tar -xf - -C "$TMP/src"
  if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
    if cmake -S "$TMP/src" -B "$TMP/b" -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug > "$TMP/cmake.txt" 2>&1 \
       && cmake --build "$TMP/b" > "$TMP/build.txt" 2>&1; then
      W=$(grep -c -i "warning" "$TMP/build.txt")
      ok "CMake + Ninja + clang: build succeeded"
      [ "$W" -eq 0 ] && ok "no compiler warnings" || { warn "$W warning line(s)"; grep -i warning "$TMP/build.txt" | head -5 | sed 's/^/         /'; }
    else
      bad "CMake build with clang failed"; tail -12 "$TMP/cmake.txt" "$TMP/build.txt" 2>/dev/null | sed 's/^/         /'
    fi
  fi
  if command -v make >/dev/null 2>&1; then
    if ( cd "$TMP/src" && make clean >/dev/null 2>&1; make CC=clang > "$TMP/make.txt" 2>&1 ); then
      W=$(grep -c -i "warning" "$TMP/make.txt")
      ok "Makefile with CC=clang: build succeeded"
      [ "$W" -eq 0 ] && ok "no compiler warnings" || { warn "$W warning line(s)"; grep -i warning "$TMP/make.txt" | head -5 | sed 's/^/         /'; }
    else
      bad "make CC=clang failed"; tail -12 "$TMP/make.txt" | sed 's/^/         /'
    fi
    if command -v gcc >/dev/null 2>&1; then
      if ( cd "$TMP/src" && make CC=gcc > "$TMP/makegcc.txt" 2>&1 ); then
        W=$(grep -c -i "warning" "$TMP/makegcc.txt")
        ok "Makefile with CC=gcc: build succeeded (gcc stays supported)"
        [ "$W" -eq 0 ] && ok "no compiler warnings with gcc" || { warn "$W gcc warning line(s)"; grep -i warning "$TMP/makegcc.txt" | head -5 | sed 's/^/         /'; }
      else
        bad "make CC=gcc failed"; tail -12 "$TMP/makegcc.txt" | sed 's/^/         /'
      fi
    else
      warn "gcc not found; gcc compatibility was not checked (sudo apt install build-essential)"
    fi
  fi
else
  note "skipped: no project found in '$ROOT' (needs CMakeLists.txt, Makefile and server/src)."
  note "Run the script from the project folder or pass its path as the first argument."
fi

# ---------------------------------------------------------------------------
echo
echo "PASS=$PASS  WARN=$WARN  FAIL=$FAIL"
if [ "$FAIL" -eq 0 ]; then
  echo "Result: READY. The required parts work; WARN lines are optional."
  exit 0
else
  echo "Result: NOT READY. Fix the FAIL lines above and run the script again."
  exit 1
fi
