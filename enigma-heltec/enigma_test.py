#!/usr/bin/env python3
# Copyright (C) 2025 Mike Barnett / Badger Works
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Badger Works Enigma Engine - Python Test Harness
Mirrors ESP32 C++ implementation exactly.
Range: printable ASCII 32-125 (94 chars, even - zero fixed points guaranteed)
Tilde ~ (126) excluded.

Usage:
    python3 enigma_test.py              # run self tests
    python3 enigma_test.py "your text"  # encrypt a specific message
    python3 enigma_test.py -i           # interactive mode
"""

import sys

PRINTABLE_START = 32
PRINTABLE_END   = 125
PR              = 94
ROTOR_PERIOD_M  = 94
ROTOR_PERIOD_L  = 8836  # 94*94

ROTOR_WIRINGS = [
    # Rotor L
    [
         70,  59,  61,  62,  33,  26,   1,  67,  15,  60,  19,  10,  52,  73,  51,  87,
         49,  41,  76,  30,  55,  74,  45,  32,  79,  66,  89,   8,  37,  83,  78,  47,
         44,  42,  92,  50,  93,  12,  36,  23,  39,  40,  18,  63,  72,  56,   7,  34,
         77,  46,   2,  16,  38,  65,  22,  58,  24,   5,   6,  21,  48,  86,   9,  68,
         43,  82,  20,   0,  90,  57,  88,  53,  85,  25,  71,  80,  64,  29,  27,  84,
         91,   4,  54,  75,  11,  69,  13,  17,  28,  31,  35,   3,  14,  81,
    ],
    # Rotor M
    [
         41,  14,  13,  63,  23,  10,  29,  52,   2,  56,  77,  35,  30,  47,  38,  16,
         93,   9,  85,  76,  46,  57,  86,  27,  72,  49,  80,   6,  70,  59,   5,  91,
         83,  19,  39,   3,  79,  55,  54,  67,  32,  53,  43,   1,  22,  17,  75,  18,
          7,  25,  20,  48,  65,  64,  58,  51,  24,  28,  50,   0,  78,  84,  69,  34,
         15,  61,   8,  11,  42,  66,  40,  90,  31,  60,  82,  68,  26,  71,  73,  92,
         37,  12,   4,  33,  45,  44,  36,  89,  21,  62,  81,  87,  88,  74,
    ],
    # Rotor R
    [
          7,   1,  36,  44,  23,  80,  92,  38,  51,  40,  81,  24,  53,  56,  27,  54,
         58,  82,  35,  71,  91,  45,  87,  43,  73,  52,  72,  31,  30,  29,  19,  61,
         83,  16,  20,  46,  69,  11,  13,   6,   2,  10,  75,  12,  33,  17,  86,  15,
         76,  37,  57,  88,  22,  49,   9,  64,  47,  39,  78,  90,  89,  14,  34,  18,
          8,  66,  74,  60,  59,  50,  70,  85,   4,  28,  93,  55,  41,   5,  48,  77,
         84,  26,  68,  32,  25,   0,  21,  65,  62,  79,  67,  63,   3,  42,
    ],
]

REFLECTOR = [
         69,  70,  92,  25,  30,  56,  90,  15,  65,  34,  86,  57,  24,  55,  74,   7,
         61,  89,  45,  33,  71,  87,  75,  42,  12,   3,  64,  36,  66,  51,   4,  58,
         50,  19,   9,  88,  27,  49,  77,  91,  82,  67,  23,  46,  47,  18,  43,  44,
         76,  37,  32,  29,  78,  60,  93,  13,   5,  11,  31,  81,  53,  16,  68,  79,
         26,   8,  28,  41,  62,   0,   1,  20,  73,  72,  14,  22,  48,  38,  52,  63,
         84,  59,  40,  85,  80,  83,  10,  21,  35,  17,   6,  39,   2,  54,
]

ROTOR_STARTS = [42, 17, 93]
PLUGBOARD    = list(range(PR))


class Rotor:
    def __init__(self, wiring, start_offset):
        self.start_offset = start_offset % PR
        self.offset = self.start_offset
        self.wiring = [w % PR for w in wiring]
        self.inverse = [0] * PR
        for i in range(PR):
            self.inverse[self.wiring[i]] = i

    def reset(self):
        self.offset = self.start_offset

    def advance(self):
        self.offset = (self.offset + 1) % PR

    def forward(self, val):
        shifted = (val + self.offset) % PR
        return (self.wiring[shifted] - self.offset + PR) % PR

    def reverse(self, val):
        shifted = (val + self.offset) % PR
        return (self.inverse[shifted] - self.offset + PR) % PR


class Enigma:
    def __init__(self):
        self.rotors    = [Rotor(ROTOR_WIRINGS[i], ROTOR_STARTS[i]) for i in range(3)]
        self.plugboard = [p % PR for p in PLUGBOARD]
        self.reflector = [r % PR for r in REFLECTOR]
        self.char_count = 0

    def reset(self):
        for r in self.rotors: r.reset()
        self.char_count = 0

    def _advance(self):
        self.rotors[2].advance()
        if self.char_count % ROTOR_PERIOD_M == 0: self.rotors[1].advance()
        if self.char_count % ROTOR_PERIOD_L == 0: self.rotors[0].advance()
        self.char_count += 1

    def process_char(self, c):
        val = (ord(c) - PRINTABLE_START) % PR
        val = self.plugboard[val]
        val = self.rotors[2].forward(val)
        val = self.rotors[1].forward(val)
        val = self.rotors[0].forward(val)
        val = self.reflector[val]
        val = self.rotors[0].reverse(val)
        val = self.rotors[1].reverse(val)
        val = self.rotors[2].reverse(val)
        val = self.plugboard[val]
        self._advance()
        return chr(val + PRINTABLE_START)

    def process_string(self, text):
        self.reset()
        return ''.join(self.process_char(c) for c in text)

    def get_rotor_offsets(self):
        return tuple(r.offset for r in self.rotors)


PASS = "\033[92mPASS\033[0m"
FAIL = "\033[91mFAIL\033[0m"
SEP  = "-" * 52

def run_self_tests():
    enigma  = Enigma()
    all_ok  = True

    print(SEP)
    print("  BADGER WORKS ENIGMA ENGINE v0.3 - Test Harness")
    print("  Range: ASCII 32-125 (94 chars) - zero fixed points")
    print(SEP)

    print("\nRoundtrip tests:")
    cases = [
        "HELLO MIKE",
        "THE QUICK BROWN FOX",
        "Badger Works 2025!",
        "0123456789",
        "!@#$%^&*()",
        "The quick brown fox jumps over the lazy dog",
        "short",
        "A",
        " ",
        "Mixed CASE 123 !@#",
        "ESP32 -> LoRa -> Enigma",
        "BW Enigma / LoRa",
    ]
    for c in cases:
        enc = enigma.process_string(c)
        dec = enigma.process_string(enc)
        ok  = dec == c
        print(f"  [{'PASS' if ok else 'FAIL'}] {c}")
        if not ok:
            print(f"         ENC: {enc}")
            print(f"         DEC: {dec}")
        all_ok &= ok

    print("\nCipher property tests:")

    # Zero fixed points guaranteed with even range
    fixed = []
    for i in range(PR):
        c = chr(i + PRINTABLE_START)
        enigma.reset()
        enc = enigma.process_char(c)
        if enc == c:
            fixed.append(repr(c))
    status = PASS if len(fixed) == 0 else FAIL
    print(f"  [{status}] Fixed points at pos 0: {fixed if fixed else 'NONE'}")
    all_ok &= len(fixed) == 0

    # Symmetry
    sym_ok = True
    for i in range(PR):
        c = chr(i + PRINTABLE_START)
        enigma.reset()
        enc = enigma.process_char(c)
        enigma.reset()
        dec = enigma.process_char(enc)
        if dec != c:
            print(f"  [{FAIL}] Symmetry broken at {repr(c)}")
            sym_ok = False
    if sym_ok:
        print(f"  [{PASS}] Symmetry verified for all {PR} chars")
    all_ok &= sym_ok

    # Tilde excluded
    enigma.reset()
    enc = enigma.process_char('}')  # 125, last valid char
    ok = 32 <= ord(enc) <= 125
    print(f"  [{'PASS' if ok else 'FAIL'}] Output stays within 32-125 range")
    all_ok &= ok

    # Rotor advance
    enigma.reset()
    off_before = enigma.get_rotor_offsets()
    for _ in range(ROTOR_PERIOD_M):
        enigma.process_char('A')
    off_after = enigma.get_rotor_offsets()
    mid_adv = off_after[1] != off_before[1]
    print(f"  [{'PASS' if mid_adv else 'FAIL'}] Middle rotor advanced after {ROTOR_PERIOD_M} chars")
    all_ok &= mid_adv

    print()
    print(SEP)
    print("  ALL TESTS PASSED - safe to flash" if all_ok else "  SOME TESTS FAILED")
    print(SEP)
    return all_ok


OLED_CHARS_PER_LINE = 21
MAX_WRAP_LINES      = 8

def sanitize_input(text):
    result = []
    last_was_space = True
    for c in text:
        val = ord(c)
        if c in ('\t', '\n', '\r'):
            c = ' '
            val = 32
        if 32 <= val <= 125:  # tilde excluded
            if c == ' ':
                if not last_was_space:
                    result.append(' ')
                    last_was_space = True
            else:
                result.append(c)
                last_was_space = False
    return ''.join(result).rstrip(' ')

def word_wrap(text, line_width=OLED_CHARS_PER_LINE):
    lines = []
    line_start = 0
    length = len(text)
    while line_start < length and len(lines) < MAX_WRAP_LINES:
        if length - line_start <= line_width:
            lines.append(text[line_start:])
            break
        break_at = -1
        for i in range(line_start + line_width - 1, line_start - 1, -1):
            if text[i] == ' ':
                break_at = i
                break
        if break_at == -1:
            lines.append(text[line_start:line_start + line_width])
            line_start += line_width
        else:
            lines.append(text[line_start:break_at])
            line_start = break_at + 1
    return lines

def run_utils_tests():
    print(SEP)
    print("  Utils tests")
    print(SEP)
    all_ok = True

    print("\nSanitizer:")
    san_cases = [
        ("Hello World",       "Hello World"),
        ("Hello\nWorld",      "Hello World"),
        ("Hello\tWorld",      "Hello World"),
        ("  Hello  World  ",  "Hello World"),
        ("Hello\x01World",    "HelloWorld"),
        ("Hello~World",       "HelloWorld"),   # tilde stripped
        ("",                  ""),
        ("   ",               ""),
    ]
    for inp, expected in san_cases:
        got = sanitize_input(inp)
        ok  = got == expected
        print(f"  [{'PASS' if ok else 'FAIL'}] {repr(inp)} -> {repr(got)}")
        if not ok:
            print(f"         expected: {repr(expected)}")
        all_ok &= ok

    print(f"\nWord wrap ({OLED_CHARS_PER_LINE} chars/line):")
    for msg in ["The quick brown fox jumps over the lazy dog",
                "Hello world", "Badger Works node online",
                "superlongwordthatwontfitononelineatall"]:
        lines = word_wrap(msg)
        print(f"  \"{msg}\"")
        for i, l in enumerate(lines):
            too_long = len(l) > OLED_CHARS_PER_LINE
            print(f"    [{i}] \"{l}\"{'  *** TOO LONG' if too_long else ''}")
            if too_long: all_ok = False

    print()
    print(SEP)
    print("  Utils: ALL PASSED" if all_ok else "  Utils: SOME FAILED")
    print(SEP)
    return all_ok

def interactive_mode():
    enigma = Enigma()
    print(SEP)
    print("  BADGER WORKS ENIGMA v0.3 - Interactive")
    print("  Commands: !debug  !reset  !quit")
    print(SEP)
    while True:
        try:
            text = input("\n> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            break
        if not text: continue
        if text == "!quit": break
        if text == "!debug":
            l,m,r = enigma.get_rotor_offsets()
            print(f"  Offsets L:{l} M:{m} R:{r}")
            continue
        if text == "!reset":
            enigma.reset()
            print("  Rotors reset.")
            continue
        clean = sanitize_input(text)
        if not clean:
            print("  (empty after sanitize)")
            continue
        enc = enigma.process_string(clean)
        dec = enigma.process_string(enc)
        print(f"  PLAIN:    {clean}")
        print(f"  CIPHER:   {enc}")
        print(f"  DECRYPT:  {dec}")
        print(f"  ROUNDTRIP: {'PASS' if dec==clean else '*** FAIL ***'}")
        print(f"  WRAPPED:")
        for i,l in enumerate(word_wrap(dec)):
            print(f"    [{i}] {l}")

if __name__ == "__main__":
    if len(sys.argv) == 1:
        run_self_tests()
        print()
        run_utils_tests()
    elif sys.argv[1] == "-i":
        interactive_mode()
    else:
        enigma = Enigma()
        text   = sanitize_input(" ".join(sys.argv[1:]))
        enc    = enigma.process_string(text)
        dec    = enigma.process_string(enc)
        print(f"PLAIN:   {text}")
        print(f"CIPHER:  {enc}")
        print(f"DECRYPT: {dec}")
        print(f"MATCH:   {'YES' if dec==text else 'NO'}")
        for i,l in enumerate(word_wrap(dec)):
            print(f"  [{i}] {l}")
