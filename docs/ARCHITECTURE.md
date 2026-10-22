# sim_iso8583: architecture and logic

Audience: you, while you write the C server. Everything here is deliberately explicit, including things you may already know, so you can check each decision against the code you write. The section numbers are referenced from the TODO comments in the C files.

---

## 1. Purpose and scope

The project, named `sim_iso8583`, simulates the communication between a payment terminal (client) and a bank (server) using ISO 8583 messages over TCP.

- **Server** (`server/`, C99, Linux; the program is built as `sim_iso8583d`, the letter d marks a server process): listens on TCP port 8583, reads framed ISO 8583 messages, applies simple business rules, and sends a reply.
- **Client** (`tools/throwaway_client.py`, Python): a test tool for Day 1. It sends 20 prepared scenarios and checks the replies. The final client (Day 2) will be built from it.

Supported messages:

| Request | Reply | Meaning |
|---|---|---|
| 0800 | 0810 | Network management: "are you there?" |
| 0200 | 0210 | Financial request: a purchase authorization |

Deliberately left out (to keep the project small): secondary bitmap (fields 65-128), reversals (0400/0410), binary or BCD field encodings, MAC and PIN fields, encryption, TLS, timeouts on idle connections, duplicate detection. You can name these as known limits in the interview.

---

## 2. System overview

```
   CLIENT (Python)                          SERVER HOST (Linux)
  +-----------------+                  +---------------------------------------------+
  |                 |                  |                                             |
  | scenario list   |                  |   PARENT process  (main.c)                  |
  | encode message  |   TCP port 8583  |   - socket / bind / listen                  |
  | add 2-byte      | ===============> |   - accept() in a loop                      |
  |   length header |                  |   - fork() once per accepted connection     |
  | send            |                  |   - reaps finished children (SIGCHLD)       |
  |                 |                  |        |                                    |
  | receive         | <=============== |        | fork()                             |
  | decode reply    |                  |        v                                    |
  | compare with    |                  |   CHILD process, one per connection         |
  |   expectation   |                  |   (connection.c: serve_client)             |
  +-----------------+                  |     loop:                                   |
                                       |       read_frame   (framing.c)              |
                                       |       iso_parse    (sim_iso8583.c)              |
                                       |       handle_message (handler.c)            |
                                       |       iso_build    (sim_iso8583.c)              |
                                       |       write_frame  (framing.c)              |
                                       |                                             |
                                       |   Day 4: LOGGER process <- pipe <- children |
                                       |   Day 5 option: PostgreSQL via ecpg         |
                                       +---------------------------------------------+
```

The design uses three main ideas:

1. **TCP gives a stream of bytes, not messages.** The program must find message boundaries itself. That is the framing layer (section 5).
2. **A message is bytes with a fixed structure.** Parsing turns bytes into a C struct, building turns the struct into bytes (section 6).
3. **Business rules work only on the struct.** They never see bytes or sockets (section 7). This makes them easy to test and easy to move to another transport.

---

## 3. Source files and layers

```
  main.c         listening socket, accept loop, fork, signal handling
     |
     v
  connection.c   what one child process does for one client (the request loop)
     |
     +--> framing.c     bytes on the socket  <-> one message (length header)
     +--> sim_iso8583.c     one message (bytes)  <-> struct iso_msg
     +--> handler.c     struct iso_msg (request) -> struct iso_msg (reply)
```

| File | Responsibility | Knows about sockets? | Knows about ISO 8583? |
|---|---|---|---|
| `main.c` | Process management | yes | no |
| `connection.c` | Order of the steps for one connection | file descriptor only | calls into it |
| `framing.c` | Read and write exactly n bytes, 2-byte length header | yes | only `ISO_MAX_MSG` |
| `sim_iso8583.c` | Field table, parse, build | no | yes |
| `handler.c` | Decision rules, response codes | no | uses `iso_msg` |

The rule that keeps this clean: **a file may only use the files below it in the diagram.** `sim_iso8583.c` never calls `read()`. `handler.c` never sees a byte buffer. If you find yourself breaking this, the code is probably in the wrong file.

---

## 4. Process model: one process per connection

### 4.1 Parent

```
  create_listener(port)             socket, SO_REUSEADDR, bind, listen
  loop forever:
      cfd = accept(lfd)             blocks until a client connects
      pid = fork()
        child  (pid == 0):  close(lfd); serve_client(cfd); close(cfd); _exit(0)
        parent (pid  > 0):  close(cfd); go back to accept()
```

After `fork()` both processes have copies of both descriptors:

- The **child** must close the *listening* socket. It never accepts.
- The **parent** must close the *connected* socket. It never talks to this client.

If either close is missing: the parent runs out of descriptors after enough connections, or the client never sees the connection end (the connection stays open while any process still holds a descriptor for it).

### 4.2 Finished children

A child that has exited stays in the process table until the parent reads its exit status. Such an entry is called a zombie process. It uses no memory, but it uses a process table entry, and the table is finite.

The parent receives the signal SIGCHLD whenever a child ends. The handler calls `waitpid(-1, NULL, WNOHANG)` in a loop until it returns 0 or -1. A loop is needed because signals of the same kind are not queued: if three children end at about the same time, the handler may run only once.

Related details (all in TODO 1 and 3 in `main.c`):

- `accept()` can return -1 with `errno == EINTR` when a signal arrives. This is not an error. Call `accept()` again.
- Use `sigaction()` with `SA_RESTART`, not `signal()`. The behavior of `signal()` differs between UNIX systems, which matters for a later move between AIX and Linux.
- Ignore SIGPIPE. Writing to a socket that the peer has closed would otherwise end the process.
- The child ends with `_exit(0)`, not `exit(0)`, so it does not flush stdio buffers that were copied from the parent.

### 4.3 Why fork per connection

| | fork per connection (chosen) | poll/select loop | threads |
|---|---|---|---|
| Code size | Smallest: the child code is plain sequential code | Larger: you keep state per connection | Medium |
| Isolation | A crash in one child affects only that client | One crash ends all clients | One crash ends all clients |
| Cost per connection | High (a process) | Low | Medium |
| Many idle connections | Poor | Good | Medium |
| Sharing data between clients | Needs IPC (pipe, shared memory) | Direct | Direct, needs locks |
| Fit with this project | The logger pipe on Day 4 is a natural IPC exercise | Possible later | Not wanted |

Answer for the interview: payment hosts of this style handle a moderate number of long-lived connections. Process isolation means a bug in the handling of one message cannot affect other clients. The cost of a process is acceptable at this scale. If the number of connections reached thousands, I would change to poll/epoll with a worker pool, because processes would use too much memory.

---

## 5. Framing: finding message boundaries in a byte stream

### 5.1 The problem

TCP delivers bytes in order, without message boundaries. If the client calls `send()` once with 72 bytes, the server's `read()` may return 72 bytes, or 30 bytes and then 42 bytes, or 1 byte at a time. If the client calls `send()` twice, one `read()` may return both messages joined. The program cannot assume anything about how the bytes are split.

### 5.2 The solution used here

Every message on the wire is preceded by a 2-byte length, most significant byte first (big-endian, also called network byte order). The length counts the bytes after the header and does not include the header.

```
  +--------+--------+----------------------------------+
  | len hi | len lo |  message (len bytes)             |
  +--------+--------+----------------------------------+
   2 bytes            0x0048 = 72 bytes in the example
```

The reader does two exact reads:

```
  read_exact(fd, hdr, 2)        -> len = (hdr[0] << 8) | hdr[1]
  check 0 < len <= ISO_MAX_MSG  -> otherwise stop, before reading the body
  read_exact(fd, buf, len)      -> the message
```

`read_exact` is a loop around `read()` that continues until all requested bytes have arrived (section 5.3). Because the reader asks for exactly the number of bytes it needs, it never reads into the next message. Two messages sent together are therefore separated correctly with no extra buffer logic.

### 5.3 `read_exact` outcomes

| `read()` returns | Meaning | Action |
|---|---|---|
| > 0 | some bytes arrived | add to the count, continue until n |
| 0 | peer closed the connection | if nothing was read yet: clean end (return 0). If some bytes were read: truncated message (return -1) |
| -1, `EINTR` | a signal interrupted the call | try again |
| -1, other | error (for example connection reset) | return -1 |

The difference between "closed before the first byte" and "closed in the middle" matters: the first is a client that finished normally, the second is a damaged exchange.

### 5.4 Length limit

The check `len > ISO_MAX_MSG` happens before the body is read. Without it a client could send the header `FF FF`, and the server would wait for 65535 bytes. Scenarios `bad_length_too_big` and `bad_length_zero` test this.

---

## 6. ISO 8583 message format (as used in this project)

### 6.1 Overall layout

```
  +-----------+----------------+---------------------------------------+
  | MTI       | primary bitmap | data fields, ascending field number   |
  | 4 ASCII   | 8 raw bytes    | only those whose bit is set           |
  | digits    | (64 bits)      |                                       |
  +-----------+----------------+---------------------------------------+
```

All field *values* are ASCII text. The bitmap is the only binary part.

### 6.2 MTI (message type indicator)

Four digits. Each digit has a meaning:

```
   0   2   0   0
   |   |   |   +-- origin:   0 = acquirer (the terminal side)
   |   |   +------ function: 0 = request, 1 = request response
   |   +---------- class:    2 = financial, 8 = network management
   +-------------- version:  0 = ISO 8583:1987
```

So 0200 is a financial request, 0210 is its response, 0800 is a network management request, 0810 is its response. A response MTI is the request MTI plus 10.

### 6.3 Bitmap

64 bits in 8 bytes. Bit 1 is the most significant bit of byte 0. Bit n set means field n is present.

```
  bit position:   byte index = (n - 1) / 8      mask = 0x80 >> ((n - 1) % 8)
```

Examples of the mask: field 2 -> byte 0, 0x40. Field 7 -> byte 0, 0x02. Field 11 -> byte 1, 0x20. Field 41 -> byte 5, 0x80.

Bit 1 has a special meaning in the standard: it announces a second bitmap (fields 65-128). This simulator does not support it, so a message with bit 1 set is rejected.

### 6.4 Field table (this must stay identical in C and Python)

| No. | Name | Format | Length | Content |
|---|---|---|---|---|
| 2 | PAN (card number) | LLVAR, digits | 2-digit length prefix, then 1..19 digits | `16` + `4111111111111111` |
| 3 | Processing code | fixed, digits | 6 | `000000` = purchase |
| 4 | Amount | fixed, digits | 12 | minor units: `000000001000` = 10.00 |
| 7 | Transmission date and time | fixed, digits | 10 | `MMDDhhmmss` |
| 11 | STAN (system trace audit number) | fixed, digits | 6 | sequence number chosen by the client |
| 37 | Retrieval reference number | fixed, any characters | 12 | set by the server on approval |
| 39 | Response code | fixed, any characters | 2 | set by the server |
| 41 | Terminal ID | fixed, any characters | 8 | `TERM0001` (padded with spaces if shorter) |

"LLVAR" means: two ASCII digits give the length of the value, then the value follows. Fixed fields have no prefix.

### 6.5 Worked example: a purchase request, byte by byte

Request: card 4111111111111111, purchase, amount 10.00, date/time 22 Oct 12:00:00, STAN 42, terminal TERM0001.

```
  00 48                                   length header: 0x0048 = 72 bytes follow
  30 32 30 30                             MTI "0200"
  72 20 00 00 00 80 00 00                 bitmap
        72 = 0100 0010 -> fields 2, 3, 4, 7   (0x40 + 0x20 + 0x10 + 0x02)
        20 = 0010 0000 -> field 11            (second byte: fields 9..16)
        80 = 1000 0000 -> field 41            (sixth byte: fields 41..48)
  31 36 | 34 31 31 31 31 31 31 31 31 31 31 31 31 31 31 31    F2  "16" + "4111111111111111"
  30 30 30 30 30 30                       F3  "000000"
  30 30 30 30 30 30 30 30 31 30 30 30     F4  "000000001000"
  31 30 32 32 31 32 30 30 30 30           F7  "1022120000"
  30 30 30 30 34 32                       F11 "000042"
  54 45 52 4D 30 30 30 31                 F41 "TERM0001"
```

Size check: 4 + 8 + 18 + 6 + 12 + 10 + 6 + 8 = 72 = 0x48.

The approved reply (86 bytes, header `00 56`) has MTI `0210`, the same fields, and two more: field 37 (`120000000042`) and field 39 (`00`). Its bitmap is `72 20 00 00 0A 80 00 00`: the fifth byte `0A` sets field 37 (0x08) and field 39 (0x02).

You can print this breakdown for every test message without a server: `python3 tools/throwaway_client.py --dump`.

### 6.6 Parsing: what can be wrong

The parser must check every item below and return an error code. The server's reaction to any parser error is the same: close the connection without a reply (section 8).

| Check | Error code |
|---|---|
| fewer than 12 bytes | `ISO_ERR_SHORT` |
| MTI not 4 digits | `ISO_ERR_BAD_MTI` |
| bit 1 set | `ISO_ERR_SECONDARY` |
| bit set for a field not in the table | `ISO_ERR_UNKNOWN_FIELD` |
| fixed field: fewer bytes left than its length | `ISO_ERR_BAD_FIELD` |
| LLVAR: prefix missing, not digits, 0, or above the maximum | `ISO_ERR_BAD_FIELD` |
| digits-only field has other characters | `ISO_ERR_BAD_FIELD` |
| bytes left after the last field | `ISO_ERR_BAD_FIELD` |

Every offset must be checked against the message length *before* reading. The input comes from the network and cannot be trusted. A missing check is the classic cause of buffer over-reads in this kind of code.

### 6.7 Building

Building is the reverse: write the MTI, compute the bitmap from `present[]`, append the fields in ascending order. Never write the length header here; `write_frame` does that. Check the output capacity before each write.

---

## 7. Server logic: business rules

### 7.1 Dispatch

```
  MTI 0800  -> reply 0810, field 39 = "00"
  MTI 0200  -> apply the rules below, reply 0210
  other     -> no reply, close the connection
```

### 7.2 Reply content

The reply starts as a copy of the request. The MTI changes (+10). Field 39 is set. On an approval, field 37 is set as well. All other request fields are returned unchanged. This is what lets the client match a reply to its request: the STAN (field 11) comes back the same.

### 7.3 Decision table for 0200

Check the rules **in this order**. The first rule that matches decides the response code.

| # | Condition | Code | Meaning |
|---|---|---|---|
| 1 | one of fields 2, 3, 4, 7, 11, 41 is missing | 30 | format error |
| 2 | field 3 is not `000000` | 12 | invalid transaction |
| 3 | PAN is not 13-19 digits, or fails the Luhn check | 14 | invalid card number |
| 4 | amount is 0 | 13 | invalid amount |
| 5 | amount is above 500000 (5000.00) | 61 | exceeds amount limit |
| 6 | PAN is `4000000000000002` | 05 | do not honour |
| 7 | PAN is `4000000000009995` | 51 | insufficient funds |
| 8 | none of the above | 00 | approved |

The order is part of the specification. Example: a message with a missing terminal ID and a bad card number gets code 30, not 14, because rule 1 comes first. The test scenarios assume this order.

### 7.4 Luhn check

Used by card numbers to detect typing errors. Starting from the rightmost digit, going left: every second digit (the second, fourth, and so on from the right) is doubled; if the result is above 9, subtract 9; add all the digits; the card number is valid if the sum is divisible by 10.

Test values: `4111111111111111` is valid, `4111111111111112` is not.

### 7.5 Amount and type size

Field 4 can hold up to 12 digits, so up to 999,999,999,999. A 32-bit signed integer holds at most 2,147,483,647. Use `unsigned long long` (or `uint64_t`) and `strtoull`. See section 11.

### 7.6 Retrieval reference number (field 37)

Any 12 characters are accepted by the client script. Suggested: 6 digits from `time(NULL) % 1000000` followed by the 6 digits of the STAN. It is not guaranteed unique; real hosts use a sequence. Out of scope.

---

## 8. Error handling summary

| Situation | Server action | Client sees |
|---|---|---|
| Valid 0800 | reply 0810 / 00 | reply |
| Valid 0200, any decision | reply 0210 with the code | reply |
| 0200 with a missing required field | reply 0210 / 30 | reply |
| Length header 0 or above `ISO_MAX_MSG` | close, no reply | end of connection |
| Parser error (section 6.6) | close, no reply | end of connection |
| Valid message with unsupported MTI (for example 0100) | close, no reply | end of connection |
| Client closes between messages | child ends normally | nothing |
| Client closes in the middle of a message | child ends, logs it | nothing |

Why "close without reply" for unparseable input: the server cannot trust anything in a message it cannot parse, including the STAN, so it cannot build a correct reply. Many real hosts do the same. A reply format for such cases is a possible extension.

Note for testing: a server that closes every connection would pass the eight `bad_*` scenarios. Only the scenarios that expect a reply prove that the parsing and the rules work. Check the `bad_*` results only after the reply scenarios pass.

---

## 9. Message sequences

### 9.1 Network check

```
  Client                                   Server child
    | -- 00 1C | 0800 | F7 F11 ----------->  |
    |                                         |  read_frame, iso_parse, handle_message:
    |                                         |  MTI 0800 -> 0810, F39 = 00
    | <---------- 00 1C | 0810 | F7 F11 F39 --|
```

### 9.2 Approved purchase

```
  Client                                   Server child
    | -- 0200 F2 F3 F4 F7 F11 F41 ---------> |
    |                                        |  rules 1..7 do not match
    |                                        |  rule 8: code 00, make field 37
    | <------ 0210 F2 F3 F4 F7 F11 F37 F39 F41 (F39 = 00)
```

### 9.3 Declined purchase

```
    | -- 0200 (PAN 4000000000000002) ------> |
    |                                        |  rule 6 matches: code 05
    | <------ 0210 ... F39 = 05  (no field 37)
```

### 9.4 Malformed message

```
    | -- 00 18 | 0200 | bitmap with field 5 -> |
    |                                         |  iso_parse: ISO_ERR_UNKNOWN_FIELD
    |                                         |  log the error, return from serve_client
    |                                         |  close(cfd), child exits
    | <-- end of connection (recv returns 0) -|
```

### 9.5 Connection lifetime

```
  Client connect() ---------------------> parent accept() -> fork() -> child created
  Client sends request 1  <-> reply 1      \
  Client sends request 2  <-> reply 2       }  child loops in serve_client
  ...                                      /
  Client close() ----------------------> child: read_frame returns FRAME_CLOSED -> exit
                                           parent: SIGCHLD -> waitpid
```

---

## 10. Suggested order of work and what to check at each step

Build with `make`. Start the server with `make run` in one terminal. Run the client in a second terminal: `python3 tools/throwaway_client.py`. Use `--only <text>` to run one scenario.

| Step | Write | Expected result |
|---|---|---|
| 1 | `create_listener`, accept loop (no fork yet; call `serve_client` directly), `install_sigchld_handler` | Client prints results instead of "Cannot connect". Everything fails, but connections are accepted |
| 2 | `read_exact`, `read_frame`, `write_all`, and the first part of `serve_client` with `hex_dump("rx", ...)` | Server log shows the bytes of each request. Compare with `--dump` |
| 3 | `fork` (parent/child close rules from section 4.1) | Run the client twice at the same time; both work. Check there are no zombies: `ps aux | grep sim_iso8583d` |
| 4 | `iso_parse` | Add a log line with MTI and fields. Bad scenarios now close because of real parser errors |
| 5 | `iso_build`, `write_frame`, and `handle_message` for 0800 | `echo_0800` passes |
| 6 | the 0200 rules (section 7.3) | All `decline_*`, `purchase_approved` and `format_error` pass |
| 7 | check the two framing scenarios | `framing_byte_by_byte` and `framing_two_in_one_send` pass if `read_exact` is correct |

Use `valgrind ./build/debug/sim_iso8583d` or compile with `-fsanitize=address` once step 6 works; the `bad_*` scenarios are good input for finding over-reads.

---

## 11. Byte order and type sizes (the legacy exercise)

### 11.1 Correction to the earlier plan

In the earlier plan I wrote that the legacy version would fail on a big-endian system and the refactored version would pass. **That was the wrong way around.** The wire format is big-endian (most significant byte first). Code that copies wire bytes directly into integers without conversion gives the right value on a big-endian CPU (AIX on POWER) and the wrong value on a little-endian CPU (x86, your PC). So:

| Legacy version (casts, no byte-order handling) | Refactored version |
|---|---|
| works on big-endian (AIX on POWER, ppc64, s390x) | works on both |
| fails on x86 / ARM Linux | |

This is the real situation in a migration from AIX to Linux: code that worked for years on AIX stops working on Linux. If you want to show the legacy version working, run it under QEMU on ppc64 or s390x (both big-endian). The demonstration is then: legacy passes on ppc64, fails on x86; refactored passes on both.

### 11.2 Where the legacy flaws are in this project

| Place | Legacy code | What happens on x86 | Portable code |
|---|---|---|---|
| Length header, `read_frame` | `n = *(unsigned short *)hdr;` | wire `00 48` is read as 0x4800 = 18432, which is above `ISO_MAX_MSG`, so every connection is closed | `n = ((size_t)hdr[0] << 8) \| hdr[1];` |
| Length header, `write_frame` | `*(unsigned short *)hdr = (unsigned short)len;` | bytes are written in the wrong order | `hdr[0] = len >> 8; hdr[1] = len & 0xFF;` |
| Bitmap test, `iso_parse` | copy the 8 bytes into an `unsigned long long`, test with `1ULL << (64 - n)` | bit positions are mirrored per byte, wrong fields are found | test single bytes: `buf[4 + (n-1)/8] & (0x80 >> ((n-1)%8))` |
| Amount, `handle_message` | `long amount = atol(...)` | works where `long` is 64 bits (64-bit Linux); a 12-digit amount overflows where `long` is 32 bits (any 32-bit build, `gcc -m32`) | `unsigned long long` or `uint64_t` with `strtoull` |
| Overlay structs | `struct { char mti[4]; unsigned char bm[8]; } *h = (void *)buf;` | works for char-only structs; breaks as soon as an int or a different alignment is added, because the compiler inserts padding | read fields by offset, one at a time |

### 11.3 How to use it

1. Write the legacy form in `read_frame` first (it takes one line).
2. Run the client. Every scenario that expects a reply fails with "server closed the connection". Find out why with `hex_dump` and gdb. This is the debugging story for the interview.
3. Commit this state with a clear message ("legacy length decoding, works only on big-endian").
4. Fix it. Commit separately.
5. Optional: repeat for the bitmap and the amount. Each one is a separate, small commit.

Do not spend more than the time budget on QEMU. If it does not work within an hour, describe the experiment in words.

---

## 12. Working on Windows with Visual Studio 2026

### 12.1 Why a Linux toolchain is needed

The server cannot be compiled by Microsoft's C compiler (MSVC): it uses `fork()`, `sigaction()` and `<sys/socket.h>`, which Windows does not have. Visual Studio can still be used as the editor, builder and debugger, if it builds with a Linux toolchain inside Windows Subsystem for Linux (WSL 2).

### 12.2 One-time setup

1. In an administrator PowerShell: `wsl --install -d Ubuntu`. Restart if asked, then create the Linux user name and password.
2. In the Ubuntu terminal: `sudo apt update && sudo apt install build-essential clang lld lldb llvm gdb cmake ninja-build rsync zip python3 ksh valgrind git`
   (Visual Studio needs a compiler, gdb, cmake, ninja, rsync and zip inside WSL. `clang` is the default compiler, `build-essential` provides gcc and make, `lld`, `lldb` and `llvm` are the LLVM linker, debugger and symbolizer. The others are for later days.)
   Check the result with `bash tools/check_llvm.sh` from the project folder. It changes nothing and ends with READY or NOT READY.
3. In the Visual Studio Installer, choose Modify and add the workload "Linux and embedded development with C++" (the name may differ slightly in version 2026).

### 12.3 Opening the project

1. The folder with `CMakeLists.txt` and `Makefile` side by side is the project root, and it is the folder you open. Check in File Explorer that `CMakeLists.txt` is directly inside it. If you unzipped the archive into a folder that has the same name, you get `sim_iso8583\sim_iso8583`; open the inner one, or move its contents up.
2. Delete any old `out/` and `.vs/` folders (they contain a cache made for the Windows compiler). Also delete Visual Studio's old copy inside Ubuntu: `ls ~/.vs` shows the folders, then `rm -rf ~/.vs/sim_iso8583` removes the one for this project. It holds only files that Visual Studio copied there; your Windows project is not touched, and Visual Studio recreates the copy.
3. File > Open > Folder, and choose the project root.
4. In the toolbar, set the target system to **WSL: Ubuntu** and the configuration to **Linux Debug**. The configurations come from `CMakePresets.json`: Linux Debug (clang), Linux Release (clang), Linux Debug + sanitizers (clang) and Linux Debug (gcc). The Windows ones are removed on purpose.
5. Project > Delete Cache and Reconfigure. The output window should end with "CMake generation finished".
6. Build > Build All. Visual Studio copies the sources into Ubuntu, under `~/.vs/sim_iso8583`, and builds there. The program is `~/.vs/sim_iso8583/out/build/linux-debug/sim_iso8583d`; the Output window shows the exact path. Edit your files in the Windows folder, not in the copy: Visual Studio overwrites the copy at the next build.

### 12.4 Running and testing

- Build and run both work from the Ubuntu terminal as well: `make` and `make run` (the Makefile), or `cmake --preset linux-debug && cmake --build out/build/linux-debug` (the same as Visual Studio does).
- Start the server (in Visual Studio with the green start button, or in one Ubuntu terminal), then run the test client in a second Ubuntu terminal: `python3 tools/throwaway_client.py`. Your Windows drives are visible in Ubuntu under `/mnt/c/...`.
- Debugging with breakpoints in Visual Studio uses gdb inside WSL. The core-dump exercise on Day 3 is easiest in the Ubuntu terminal.

### 12.5 Compilers: clang by default, gcc supported

The default compiler is clang (LLVM). gcc stays supported because the target job uses gcc, and a program that compiles without warnings with both compilers is more portable. The Makefile and the CMake presets select the compiler:

| Task | Makefile | CMake / Visual Studio |
|---|---|---|
| Build with clang | `make` | preset "Linux Debug (clang)" |
| Build with gcc | `make CC=gcc` | preset "Linux Debug (gcc)" |
| Add AddressSanitizer + UBSan | `make SAN=1` | preset "Linux Debug + sanitizers (clang)" |

Changing `CC` or `SAN` makes the Makefile rebuild every file (it compares the compiler and flags with the last build). With CMake each preset has its own folder under `out/build/`.

The sanitizers stop the program with a report when it reads or writes outside a buffer. Use them with the `bad_*` scenarios after the parser works (Day 3). For readable reports with file names and line numbers, `llvm-symbolizer` must be installed (package `llvm`). If the sanitizer build fails to link on Ubuntu with a message about `libclang_rt`, install `libclang-rt-<version>-dev` (for example `libclang-rt-18-dev` for clang 18).

### 12.6 Known problems

- **`CMake Error: The source ".../CMakeLists.txt" does not match the source ".../CMakeLists.txt" used to generate cache.`** The CMake cache in Ubuntu was created for a different folder layout, for example before the project was moved or unzipped again. Two different `CMakeLists.txt` paths in the message confirm it. Fix: close Visual Studio; delete `out/` and `.vs/` in the Windows project folder; in Ubuntu run `rm -rf ~/.vs/sim_iso8583` (see 12.3 step 2); open the folder that directly contains `CMakeLists.txt`; choose WSL and Linux Debug; Project > Delete Cache and Reconfigure.

- If `make` reports "missing separator" or a script reports a strange `\r` character, the file has Windows line endings. Convert it in Ubuntu with `dos2unix <file>` (install with `sudo apt install dos2unix`). `.gitattributes` makes Git use Unix line endings, but an editor can still save a file with Windows endings.
- The error "needs a Linux toolchain" from CMake means the target system is still "Local Machine". Change it to WSL and reconfigure.
- Working under `/mnt/c` is slower than working in the Linux file system (for example `~/sim_iso8583`). For this project size the difference is small.
- The `Makefile` and `CMakeLists.txt` list the same source files. When you add a `.c` file, add it to both.

---

## 13. Planned later steps (where they attach)

| Step | Attachment point |
|---|---|
| Day 2: Bash and ksh scripts | call `make`, start the server in the background, run the client, stop the server |
| Day 3: core dump, gdb, Valgrind | cause a crash in `handle_message` (null pointer), `ulimit -c unlimited`, `gdb build/debug/sim_iso8583d core` |
| Day 3: portability refactor | section 11 |
| Day 4: logger process | in `serve_client` step g: write one record per transaction to a pipe. The parent creates the pipe and the logger process before the accept loop; children inherit the write end |
| Day 5 option: ecpg | the logger writes the records to PostgreSQL using embedded SQL |

---

## 14. Glossary

| Term | Meaning |
|---|---|
| ISO 8583 | Standard for messages between card payment systems |
| MTI | Message type indicator, 4 digits at the start of a message |
| Bitmap | Bits that tell which fields are present |
| PAN | Primary account number, the card number |
| STAN | System trace audit number: a number chosen by the sender to identify a request; the reply repeats it |
| RRN | Retrieval reference number (field 37) |
| LLVAR | Variable-length field with a 2-digit length prefix |
| Acquirer | The party on the merchant side (terminal, merchant bank) |
| Issuer | The bank that issued the card; this server plays the role of the issuer's host |
| Big-endian | Most significant byte first; used on the wire, also called network byte order |
| Zombie process | A process that has ended but whose exit status the parent has not yet collected |
| Framing | Marking where one message ends and the next begins in a byte stream |
