#!/usr/bin/env python3
"""
Throwaway test client for sim_iso8583, the ISO 8583 simulator (Day 1).

It has its own encoder and decoder, written separately from the C code,
so a mistake in the C parser shows up as a failing scenario.
Python 3.6 or newer, standard library only.

Usage:
    python3 tools/throwaway_client.py                  run all scenarios
    python3 tools/throwaway_client.py --dump           show the bytes of each request, no network
    python3 tools/throwaway_client.py --only luhn      run scenarios whose name contains "luhn"
    python3 tools/throwaway_client.py --list           list scenario names
    python3 tools/throwaway_client.py --port 9000 --host 127.0.0.1 --timeout 3

Exit code: 0 all passed, 1 at least one failed, 2 cannot connect.
"""

import argparse
import socket
import struct
import sys
import time

# ----------------------------------------------------------------------
# Field table. Must match SPEC[] in server/src/sim_iso8583.c.
#   number: (name, kind, size, digits_only)
# ----------------------------------------------------------------------
SPEC = {
    2:  ("PAN",                    "llvar", 19, True),
    3:  ("Processing code",        "fixed",  6, True),
    4:  ("Amount (minor units)",   "fixed", 12, True),
    7:  ("Transmission date/time", "fixed", 10, True),
    11: ("STAN",                   "fixed",  6, True),
    37: ("Retrieval reference",    "fixed", 12, False),
    39: ("Response code",          "fixed",  2, False),
    41: ("Terminal ID",            "fixed",  8, False),
}

MAX_MSG = 1024   # must match ISO_MAX_MSG in the server


# ----------------------------------------------------------------------
# Encoding and decoding
# ----------------------------------------------------------------------
def encode_message(mti, fields, validate=True):
    """MTI (4 ASCII digits) + 8-byte bitmap + fields in ascending order."""
    bitmap = bytearray(8)
    body = b""
    for n in sorted(fields):
        name, kind, size, digits = SPEC[n]
        value = fields[n]
        if validate:
            if digits and not value.isdigit():
                raise ValueError("field %d must be digits: %r" % (n, value))
            if kind == "fixed" and len(value) != size:
                raise ValueError("field %d must be %d characters" % (n, size))
            if kind == "llvar" and not 1 <= len(value) <= size:
                raise ValueError("field %d must be 1..%d characters" % (n, size))
        raw = value.encode("ascii")
        if kind == "llvar":
            raw = ("%02d" % len(raw)).encode("ascii") + raw
        bitmap[(n - 1) // 8] |= 0x80 >> ((n - 1) % 8)
        body += raw
    return mti.encode("ascii") + bytes(bitmap) + body


def frame(msg):
    """Add the 2-byte big-endian length header."""
    return struct.pack(">H", len(msg)) + msg


def decode_message(data):
    """Return (mti, {field: text}) or raise ValueError."""
    if len(data) < 12:
        raise ValueError("shorter than MTI + bitmap (12 bytes)")
    mti = data[:4].decode("ascii", "replace")
    if not (len(mti) == 4 and all(c in "0123456789" for c in mti)):
        raise ValueError("MTI is not 4 digits: %r" % mti)
    bitmap = data[4:12]
    if bitmap[0] & 0x80:
        raise ValueError("secondary bitmap is not supported")
    pos = 12
    fields = {}
    for n in range(2, 65):
        if not bitmap[(n - 1) // 8] & (0x80 >> ((n - 1) % 8)):
            continue
        if n not in SPEC:
            raise ValueError("field %d is not in the field table" % n)
        name, kind, size, digits = SPEC[n]
        if kind == "llvar":
            prefix = data[pos:pos + 2]
            if len(prefix) < 2 or not prefix.isdigit():
                raise ValueError("field %d: bad length prefix" % n)
            length = int(prefix)
            if length < 1 or length > size:
                raise ValueError("field %d: length %d out of range" % (n, length))
            pos += 2
        else:
            length = size
        raw = data[pos:pos + length]
        if len(raw) < length:
            raise ValueError("field %d: message ends too early" % n)
        text = raw.decode("ascii", "replace")
        if digits and not text.isdigit():
            raise ValueError("field %d: not all digits: %r" % (n, text))
        fields[n] = text
        pos += length
    if pos != len(data):
        raise ValueError("%d unused bytes after the last field" % (len(data) - pos))
    return mti, fields


def hexs(data):
    return " ".join("%02X" % b for b in data)


def annotate(data):
    """Return text lines that explain every part of one message."""
    lines = []
    if len(data) < 12:
        return ["(%d bytes, too short to explain)" % len(data), "  " + hexs(data)]
    lines.append("MTI     %-24s %r" % (hexs(data[0:4]), data[0:4].decode("ascii", "replace")))
    bitmap = data[4:12]
    present = [n for n in range(1, 65) if bitmap[(n - 1) // 8] & (0x80 >> ((n - 1) % 8))]
    lines.append("Bitmap  %-24s fields set: %s" % (hexs(bitmap), ", ".join(map(str, present))))
    pos = 12
    for n in present:
        if n not in SPEC:
            lines.append("  (field %d is not in the field table, stop)" % n)
            return lines
        name, kind, size, digits = SPEC[n]
        if kind == "llvar":
            prefix = data[pos:pos + 2]
            if len(prefix) < 2 or not prefix.isdigit():
                lines.append("  (field %d has a bad length prefix, stop)" % n)
                return lines
            length = int(prefix)
            raw = data[pos:pos + 2 + length]
        else:
            raw = data[pos:pos + size]
        shown = raw[2:] if kind == "llvar" else raw
        text = shown.decode("ascii", "replace")
        note = " (2-digit length prefix %s)" % raw[:2].decode("ascii") if kind == "llvar" else ""
        lines.append("F%-3d    %-24s %s = %r%s" % (n, hexs(raw)[:24] + ("..." if len(raw) > 8 else ""), name, text, note))
        pos += len(raw)
    if pos != len(data):
        lines.append("  (%d bytes after the last field)" % (len(data) - pos))
    return lines


# ----------------------------------------------------------------------
# Scenario definitions
# ----------------------------------------------------------------------
_stan_counter = [0]


def next_stan():
    _stan_counter[0] += 1
    return "%06d" % _stan_counter[0]


def now_stamp():
    return time.strftime("%m%d%H%M%S", time.gmtime())


def purchase_fields(**override):
    f = {2: "4111111111111111", 3: "000000", 4: "000000001000",
         7: now_stamp(), 11: next_stan(), 41: "TERM0001"}
    for k, v in override.items():
        n = int(k[1:])            # keyword "f4" means field 4
        if v is None:
            f.pop(n, None)
        else:
            f[n] = v
    return f


def resp(mti, code, stan):
    return ("resp", mti, code, stan)


def make_scenarios():
    sc = []

    def add(name, desc, chunks, expect):
        sc.append({"name": name, "desc": desc, "chunks": chunks, "expect": expect})

    def send_whole(msg):
        return [(frame(msg), 0)]

    # --- Normal traffic -------------------------------------------------
    f = {7: now_stamp(), 11: next_stan()}
    add("echo_0800", "Network check 0800 -> 0810 with code 00",
        send_whole(encode_message("0800", f)), [resp("0810", "00", f[11])])

    f = purchase_fields()
    add("purchase_approved", "Valid purchase -> 0210 code 00 (and a retrieval reference in field 37)",
        send_whole(encode_message("0200", f)), [resp("0210", "00", f[11])])

    f = purchase_fields(f2="4000000000000002")
    add("decline_do_not_honour", "Test card 4000000000000002 -> code 05",
        send_whole(encode_message("0200", f)), [resp("0210", "05", f[11])])

    f = purchase_fields(f2="4000000000009995")
    add("decline_no_funds", "Test card 4000000000009995 -> code 51",
        send_whole(encode_message("0200", f)), [resp("0210", "51", f[11])])

    f = purchase_fields(f2="4111111111111112")
    add("decline_bad_luhn", "PAN fails the Luhn check -> code 14",
        send_whole(encode_message("0200", f)), [resp("0210", "14", f[11])])

    f = purchase_fields(f4="000000000000")
    add("decline_zero_amount", "Amount 0 -> code 13",
        send_whole(encode_message("0200", f)), [resp("0210", "13", f[11])])

    f = purchase_fields(f4="000000500001")
    add("decline_over_limit", "Amount 5000.01 is above the limit 5000.00 -> code 61",
        send_whole(encode_message("0200", f)), [resp("0210", "61", f[11])])

    f = purchase_fields(f3="300000")
    add("decline_proc_code", "Processing code 300000 (not a purchase) -> code 12",
        send_whole(encode_message("0200", f)), [resp("0210", "12", f[11])])

    f = purchase_fields(f41=None)
    add("format_error_no_terminal", "Field 41 missing -> reply 0210 with code 30",
        send_whole(encode_message("0200", f)), [resp("0210", "30", f[11])])

    # --- Several messages, framing ----------------------------------------
    fa, fb = purchase_fields(), purchase_fields(f2="4000000000000002")
    add("two_requests_one_connection",
        "Two requests on one connection, 0.2 s apart. Both must be answered.",
        [(frame(encode_message("0200", fa)), 0.2), (frame(encode_message("0200", fb)), 0)],
        [resp("0210", "00", fa[11]), resp("0210", "05", fb[11])])

    f = purchase_fields()
    data = frame(encode_message("0200", f))
    add("framing_byte_by_byte",
        "One request sent one byte at a time. Tests that read_exact() loops.",
        [(bytes([b]), 0.01) for b in data], [resp("0210", "00", f[11])])

    fa, fb = purchase_fields(), purchase_fields()
    add("framing_two_in_one_send",
        "Two complete frames in a single send(). The server must split them correctly.",
        [(frame(encode_message("0200", fa)) + frame(encode_message("0200", fb)), 0)],
        [resp("0210", "00", fa[11]), resp("0210", "00", fb[11])])

    # --- Bad input: the server must close the connection, no reply ----------
    good = encode_message("0200", purchase_fields())
    add("bad_truncated_field", "Frame is consistent but field 41 is cut short -> connection closed",
        send_whole(good[:-3]), [("closed",)])

    bm = bytearray(8)
    bm[0] |= 0x80 >> 4        # field 5, which is not in the field table
    add("bad_unknown_field", "Bitmap marks field 5, which the table does not have -> closed",
        send_whole(b"0200" + bytes(bm) + b"000000001000"), [("closed",)])

    add("bad_secondary_bitmap", "Bit 1 set (secondary bitmap, not supported) -> closed",
        send_whole(b"0200" + b"\x80" + bytes(7) + bytes(8)), [("closed",)])

    add("bad_non_digit_amount", "Amount field contains letters -> closed",
        send_whole(encode_message("0200", purchase_fields(f4="00000000A000"), validate=False)),
        [("closed",)])

    add("bad_mti", "MTI is '02X0' -> closed",
        send_whole(b"02X0" + good[4:]), [("closed",)])

    add("bad_unsupported_mti", "Well-formed 0100 (not supported by this server) -> closed",
        send_whole(encode_message("0100", purchase_fields())), [("closed",)])

    add("bad_length_too_big", "Length header says 5000 bytes -> closed without waiting for them",
        [(struct.pack(">H", 5000) + b"0200" + bytes(8), 0)], [("closed",)])

    add("bad_length_zero", "Length header says 0 -> closed",
        [(struct.pack(">H", 0), 0)], [("closed",)])

    return sc


# ----------------------------------------------------------------------
# Running scenarios
# ----------------------------------------------------------------------
def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError
        buf += chunk
    return buf


def recv_frame(s):
    """Return ('frame', bytes), ('closed', None) or ('timeout', None)."""
    try:
        (n,) = struct.unpack(">H", recv_exact(s, 2))
        return "frame", recv_exact(s, n)
    except socket.timeout:
        return "timeout", None
    except (EOFError, ConnectionResetError, BrokenPipeError):
        return "closed", None


def check_reply(body, exp):
    _, mti, code, stan = exp
    try:
        rmti, rf = decode_message(body)
    except ValueError as e:
        return ["reply cannot be decoded: %s  [%s]" % (e, hexs(body))]
    problems = []
    if rmti != mti:
        problems.append("MTI is %s, expected %s" % (rmti, mti))
    if rf.get(39) != code:
        problems.append("field 39 is %r, expected %r" % (rf.get(39), code))
    if rf.get(11) != stan:
        problems.append("field 11 (STAN) is %r, expected %r (must be echoed)" % (rf.get(11), stan))
    if mti == "0210" and code == "00" and len(rf.get(37, "")) != 12:
        problems.append("approved reply needs field 37 with 12 characters")
    return problems


def run_scenario(host, port, timeout, sc):
    problems = []
    with socket.create_connection((host, port), timeout=timeout) as s:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.settimeout(timeout)
        try:
            for data, delay in sc["chunks"]:
                s.sendall(data)
                if delay:
                    time.sleep(delay)
        except (BrokenPipeError, ConnectionResetError):
            pass    # the server already closed; the result is checked below
        for exp in sc["expect"]:
            kind, body = recv_frame(s)
            if exp[0] == "closed":
                if kind == "closed":
                    continue
                if kind == "timeout":
                    problems.append("server neither replied nor closed within %.1f s" % timeout)
                else:
                    problems.append("server sent a reply, expected it to close: %s" % hexs(body))
            else:
                if kind == "frame":
                    problems += check_reply(body, exp)
                elif kind == "closed":
                    problems.append("server closed the connection, expected a %s reply" % exp[1])
                else:
                    problems.append("no reply within %.1f s, expected %s" % (timeout, exp[1]))
                    break
    return problems


def main():
    ap = argparse.ArgumentParser(description="Throwaway ISO 8583 test client")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8583)
    ap.add_argument("--timeout", type=float, default=2.0)
    ap.add_argument("--only", default="", help="run only scenarios whose name contains this text")
    ap.add_argument("--dump", action="store_true", help="print the request bytes and exit")
    ap.add_argument("--list", action="store_true", help="list scenarios and exit")
    args = ap.parse_args()

    scenarios = [s for s in make_scenarios() if args.only in s["name"]]
    if not scenarios:
        print("no scenario matches %r" % args.only)
        return 1

    if args.list:
        for i, sc in enumerate(scenarios, 1):
            print("%2d  %-28s %s" % (i, sc["name"], sc["desc"]))
        return 0

    if args.dump:
        for i, sc in enumerate(scenarios, 1):
            print("=== %02d %s: %s" % (i, sc["name"], sc["desc"]))
            wire = b"".join(c for c, _ in sc["chunks"])
            print("Length  %-24s = %d bytes follow" % (hexs(wire[:2]), struct.unpack(">H", wire[:2])[0]))
            for line in annotate(wire[2:]):
                print(line)
            print()
        return 0

    passed = 0
    for i, sc in enumerate(scenarios, 1):
        try:
            problems = run_scenario(args.host, args.port, args.timeout, sc)
        except (ConnectionRefusedError, socket.timeout, OSError) as e:
            print("Cannot connect to %s:%d (%s). Is the server running?" % (args.host, args.port, e))
            return 2
        if problems:
            print("[FAIL] %02d %-28s %s" % (i, sc["name"], sc["desc"]))
            for p in problems:
                print("         - " + p)
        else:
            passed += 1
            print("[PASS] %02d %-28s %s" % (i, sc["name"], sc["desc"]))
    print("\n%d of %d scenarios passed" % (passed, len(scenarios)))
    return 0 if passed == len(scenarios) else 1


if __name__ == "__main__":
    sys.exit(main())
