#!/usr/bin/env python3
"""
BW Enigma key generator.

Produces a config_user.h with a full EnigmaConfig (rotor wirings, reflector,
starting positions, plugboard) derived from a passphrase.  The same algorithm
runs on the Heltec at runtime (key_derive.h), so entering the same passphrase
in the CrowPanel settings screen produces an identical key without reflashing.

Usage:
  python3 keygen.py                        # random passphrase, derive key
  python3 keygen.py "my secret phrase"     # derive from specific passphrase
  python3 keygen.py --random               # pure random tables (max entropy)
  python3 keygen.py --out path/to/file.h   # custom output path
"""

import sys
import random
import argparse
from datetime import datetime

CIPHER_RANGE = 94


# ── Key derivation (must be identical to key_derive.h) ──────────────────────

def _fnv1a(s: str) -> int:
    """FNV-1a 32-bit hash of an ASCII string."""
    h = 0x811C9DC5
    for b in s.encode("ascii", errors="replace"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def _lcg(seed: int) -> int:
    return (seed * 1664525 + 1013904223) & 0xFFFFFFFF


def _make_permutation(seed: int):
    """Fisher-Yates shuffle of [0..93], returns (permutation, next_seed)."""
    arr = list(range(CIPHER_RANGE))
    for i in range(CIPHER_RANGE - 1, 0, -1):
        seed = _lcg(seed)
        j = seed % (i + 1)
        arr[i], arr[j] = arr[j], arr[i]
    return arr, seed


def _make_reflector(seed: int):
    """Shuffle [0..93], pair consecutive entries into a zero-fixed-point involution."""
    indices, seed = _make_permutation(seed)
    ref = [0] * CIPHER_RANGE
    for k in range(CIPHER_RANGE // 2):
        a = indices[2 * k]
        b = indices[2 * k + 1]
        ref[a] = b
        ref[b] = a
    return ref, seed


def _make_plugboard(seed: int, n_pairs: int = 10):
    """n_pairs swap pairs from another shuffle; remaining entries are identity."""
    indices, seed = _make_permutation(seed)
    plug = list(range(CIPHER_RANGE))
    for k in range(min(n_pairs, CIPHER_RANGE // 2)):
        a = indices[2 * k]
        b = indices[2 * k + 1]
        plug[a] = b
        plug[b] = a
    return plug, seed


def derive_from_passphrase(passphrase: str) -> dict:
    """Derive a complete EnigmaConfig from a passphrase."""
    seed = _fnv1a(passphrase)
    rotor_l, seed = _make_permutation(seed)
    rotor_m, seed = _make_permutation(seed)
    rotor_r, seed = _make_permutation(seed)
    reflector, seed = _make_reflector(seed)
    seed = _lcg(seed); start_l = seed % CIPHER_RANGE
    seed = _lcg(seed); start_m = seed % CIPHER_RANGE
    seed = _lcg(seed); start_r = seed % CIPHER_RANGE
    plugboard, _ = _make_plugboard(seed, 10)
    return {
        "rotor_l": rotor_l,
        "rotor_m": rotor_m,
        "rotor_r": rotor_r,
        "reflector": reflector,
        "starts": [start_l, start_m, start_r],
        "plugboard": plugboard,
    }


# ── Pure random key (not re-derivable from a passphrase) ─────────────────────

def _random_permutation() -> list:
    arr = list(range(CIPHER_RANGE))
    random.shuffle(arr)
    return arr


def _random_reflector() -> list:
    indices = list(range(CIPHER_RANGE))
    random.shuffle(indices)
    ref = [0] * CIPHER_RANGE
    for k in range(CIPHER_RANGE // 2):
        a = indices[2 * k]
        b = indices[2 * k + 1]
        ref[a] = b
        ref[b] = a
    return ref


def _random_plugboard(n_pairs: int = 10) -> list:
    indices = list(range(CIPHER_RANGE))
    random.shuffle(indices)
    plug = list(range(CIPHER_RANGE))
    for k in range(min(n_pairs, CIPHER_RANGE // 2)):
        a = indices[2 * k]
        b = indices[2 * k + 1]
        plug[a] = b
        plug[b] = a
    return plug


def random_key() -> dict:
    return {
        "rotor_l":  _random_permutation(),
        "rotor_m":  _random_permutation(),
        "rotor_r":  _random_permutation(),
        "reflector": _random_reflector(),
        "starts":   [random.randint(0, 93) for _ in range(3)],
        "plugboard": _random_plugboard(10),
    }


# ── Validation ────────────────────────────────────────────────────────────────

def validate(key: dict) -> list:
    errors = []
    for name, rotor in [("L", key["rotor_l"]), ("M", key["rotor_m"]), ("R", key["rotor_r"])]:
        if sorted(rotor) != list(range(CIPHER_RANGE)):
            errors.append(f"Rotor {name} is not a valid permutation of 0-93")
    ref = key["reflector"]
    for i in range(CIPHER_RANGE):
        if ref[ref[i]] != i:
            errors.append(f"Reflector not involution at index {i}")
        if ref[i] == i:
            errors.append(f"Reflector has fixed point at index {i}")
    plug = key["plugboard"]
    for i in range(CIPHER_RANGE):
        if plug[plug[i]] != i:
            errors.append(f"Plugboard not symmetric at index {i}")
    return errors


def fingerprint(key: dict) -> str:
    """Short FNV-1a hash of all wiring tables — paste this into the chat to
    verify both units are running the same key without revealing the key itself."""
    data = (
        bytes(key["rotor_l"])
        + bytes(key["rotor_m"])
        + bytes(key["rotor_r"])
        + bytes(key["reflector"])
    )
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return f"{h:08X}"


# ── Output ────────────────────────────────────────────────────────────────────

def _fmt_table(arr: list, comment: str) -> str:
    lines = [f"        /* {comment} */"]
    for row in range(0, CIPHER_RANGE, 16):
        chunk = arr[row : row + 16]
        nums = ", ".join(f"{v:3d}" for v in chunk)
        comma = "," if row + 16 < CIPHER_RANGE else ""
        lines.append(f"         {nums}{comma}")
    return "\n".join(lines)


def generate_header(key: dict, passphrase: str | None, mode: str) -> str:
    ts = datetime.now().strftime("%Y-%m-%d %H:%M")
    fp = fingerprint(key)
    s = key["starts"]

    lines = [
        "#pragma once",
        "/* ════════════════════════════════════════════════════════════════",
        "   BW Enigma — custom key",
    ]
    if passphrase:
        lines.append(f"   Passphrase : {passphrase}")
    else:
        lines.append("   Mode       : random (pure random — cannot be re-derived)")
    lines += [
        f"   Generated  : {ts}",
        f"   Fingerprint: {fp}",
        "",
        "   KEEP THIS FILE SECRET — it contains the complete cipher key.",
        "   Flash both Heltec units with the same config_user.h.",
        "   Alternatively, enter the passphrase in the CrowPanel settings",
        "   screen — the Heltec derives the identical key at runtime.",
        "   ════════════════════════════════════════════════════════════════ */",
        "",
        '#include "config.h"',
        "",
        "static const EnigmaConfig USER_CONFIG = {",
        "    {",
        "        {",
        _fmt_table(key["rotor_l"], "Rotor L") + ",",
        "        },",
        "        {",
        _fmt_table(key["rotor_m"], "Rotor M") + ",",
        "        },",
        "        {",
        _fmt_table(key["rotor_r"], "Rotor R") + ",",
        "        },",
        "    },",
        "    {",
        _fmt_table(key["reflector"], "Reflector") + ",",
        "    },",
        f"    {{{s[0]}, {s[1]}, {s[2]}}},",
        "    {",
        _fmt_table(key["plugboard"], "Plugboard") + ",",
        "    }",
        "};",
    ]
    return "\n".join(lines) + "\n"


# ── Main ──────────────────────────────────────────────────────────────────────

_WORDS = [
    "ALPHA", "BRAVO", "CHARLIE", "DELTA", "ECHO", "FOXTROT",
    "GOLF", "HOTEL", "INDIA", "JULIET", "KILO", "LIMA", "MIKE",
    "NOVEMBER", "OSCAR", "PAPA", "QUEBEC", "ROMEO", "SIERRA",
    "TANGO", "UNIFORM", "VICTOR", "WHISKEY", "XRAY", "YANKEE", "ZULU",
]


def main():
    parser = argparse.ArgumentParser(
        description="BW Enigma key generator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("passphrase", nargs="?",
                        help="Derive key from this passphrase")
    parser.add_argument("--random", action="store_true",
                        help="Generate pure random tables (cannot be re-derived)")
    parser.add_argument("--out", default="src/config/config_user.h",
                        help="Output path (default: src/config/config_user.h)")
    args = parser.parse_args()

    if args.random:
        key = random_key()
        passphrase = None
        print("Mode   : random (pure random tables)")
    elif args.passphrase:
        passphrase = args.passphrase
        key = derive_from_passphrase(passphrase)
        print(f"Mode   : passphrase")
        print(f"Phrase : {passphrase}")
    else:
        passphrase = "-".join(random.choices(_WORDS, k=4))
        key = derive_from_passphrase(passphrase)
        print(f"Mode   : generated passphrase")
        print(f"Phrase : {passphrase}")

    errors = validate(key)
    if errors:
        print("\nERROR: key validation failed:")
        for e in errors:
            print(f"  {e}")
        sys.exit(1)

    fp = fingerprint(key)
    s  = key["starts"]
    print(f"Fingerprint: {fp}")
    print(f"Starts : L={s[0]}  M={s[1]}  R={s[2]}")

    header = generate_header(key, passphrase, "random" if args.random else "passphrase")

    with open(args.out, "w") as f:
        f.write(header)

    print(f"\nWritten: {args.out}")
    print()

    if passphrase:
        print("To use this key without reflashing, enter the passphrase above")
        print("in the KEY PHRASE field on the CrowPanel settings screen and press SAVE.")
        print("Do this on both units — they will derive the same key at runtime.")
    else:
        print("Pure random mode: flash both Heltec units with the same config_user.h.")
        print("This key cannot be entered via the settings screen.")
    print()
    print("Both units must use identical key material to decrypt each other's messages.")


if __name__ == "__main__":
    main()
