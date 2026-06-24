# BW Enigma — Key Setup

Two units can only read each other's messages if they are running **identical key material**: the same rotor wiring tables, reflector, starting positions, and plugboard. This document covers how to set that up.

There are two paths: **runtime passphrase** (no reflash required) and **compile-time `config_user.h`** (maximum entropy, set-and-forget).

---

## The Default Key

Out of the box, all BW Enigma units share the same compiled-in key (`DEFAULT_CONFIG` in `config.h`). This is intentional — it means any two freshly flashed units can communicate immediately without any configuration. 

**Do not use the default key for anything sensitive.** It is published in the repository and anyone with the firmware binary has it.

---

## Path 1 — Runtime Passphrase (Recommended for Getting Started)

The easiest way to set a private key. No reflashing, no file editing.

### How it works

Both units derive identical key material from a shared passphrase using a deterministic algorithm (FNV-1a hash → LCG → Fisher-Yates shuffle). As long as both units get the same passphrase, they produce the same rotor wiring tables, reflector, starting positions, and plugboard.

### Steps

1. On **Unit A**, open settings (gear icon on the CrowPanel).
2. Tap the **KEY** field and type a shared passphrase — anything you like, but longer is better. Example: `NOVEMBER-FOXTROT-SIERRA-DELTA`
3. Press **SAVE**. The CrowPanel sends the passphrase to the Heltec over UART. The Heltec derives the key, stores the passphrase in NVS, and reinitializes the cipher immediately. The status bar will show `KEY:phrase`.
4. Repeat on **Unit B** with the **exact same passphrase**.
5. Send a test message. If both units decrypt correctly, the key is matched.

### To revert to the default key

Clear the KEY field and press SAVE. The Heltec discards the stored passphrase and falls back to the compiled-in default. Status bar returns to `KEY:def`.

### Verifying both units have the same key

You can generate a **fingerprint** of any passphrase without revealing the passphrase:

```bash
python3 enigma-heltec/keygen.py "your passphrase"
```

The output includes a line like `Fingerprint: A3F2C819`. Share this fingerprint over any channel (voice, text) — it tells you whether two units match without disclosing the passphrase itself.

---

## Path 2 — Compile-Time `config_user.h` (Maximum Entropy)

Use `keygen.py` to generate a random key and bake it into both firmware images. This gives you the full entropy of a randomly generated 94-character permutation, not constrained by passphrase length.

### Generate a key

```bash
cd enigma-heltec

# Option A: auto-generate a random passphrase and derive from it
python3 keygen.py
# Output: Phrase: KILO-ROMEO-TANGO-ECHO   Fingerprint: A3F2C819
# Written: src/config/config_user.h

# Option B: supply your own passphrase
python3 keygen.py "november foxtrot sierra delta"

# Option C: pure random tables (cannot be re-derived from a passphrase)
python3 keygen.py --random
```

All three write `src/config/config_user.h`. The file is gitignored — it stays local.

### Flash both units with the same key

```bash
# On the machine with config_user.h in place:
cd enigma-heltec
pio run -t upload     # flash Heltec A
# Move to the second unit, same firmware build
pio run -t upload     # flash Heltec B
```

Both units must be built and flashed from the **same `config_user.h`**. The wiring tables are compiled in; they cannot be changed without reflashing.

### Using both paths together

If a unit has a `config_user.h` baked in and also has a runtime passphrase stored in NVS, the **runtime passphrase takes priority**. Clearing the passphrase from settings reverts to the `config_user.h` key (or the default if `config_user.h` doesn't exist).

---

## Status Bar Indicator

The `KEY:` indicator in the top-right of the chat screen shows which key source is active:

| Display | Meaning |
|---|---|
| `KEY:def` | Compiled-in default key — anyone with the firmware can decrypt |
| `KEY:nvs` | NVS-stored starting positions / plugboard overlay (no passphrase) |
| `KEY:phrase` | Runtime passphrase active — derived key in NVS |

---

## Key Material Reference

A complete BW Enigma key consists of:

| Component | Size | Source |
|---|---|---|
| Rotor L wiring | 94 bytes | Compiled-in or passphrase-derived |
| Rotor M wiring | 94 bytes | Compiled-in or passphrase-derived |
| Rotor R wiring | 94 bytes | Compiled-in or passphrase-derived |
| Reflector | 94 bytes | Compiled-in or passphrase-derived |
| Starting positions (L, M, R) | 3 bytes | NVS (passphrase-derived or manual) |
| Plugboard swap table | 94 bytes | NVS (passphrase-derived or identity) |

**Wiring tables and reflector are the primary secret.** Starting positions and plugboard are additional parameters. If an attacker has the wiring tables and reflector, they can decrypt all traffic by trying the 94³ = 830,584 starting position combinations.

---

## Security Notes

- **Passphrase length matters.** A short passphrase (under 8 characters) offers little resistance. Use something long and random, like a phrase generated by `keygen.py`.
- **The passphrase is stored in NVS in plaintext** on both the Heltec and the CrowPanel. Anyone with physical access to either unit can extract it with an NVS dump. If physical security is a concern, use compile-time keys and distribute `config_user.h` only over a trusted channel, then delete it.
- **Both units must be re-keyed together.** Changing the passphrase on one unit without the other means they can no longer communicate.
- **Fingerprints help detect mismatches.** If messages come out as garbage, run `python3 keygen.py "your passphrase"` on both units and compare fingerprints.
- **This is not production-grade cryptography.** See the security disclaimer in the main README.

---

## Passphrase Derivation Algorithm

The algorithm is implemented identically in `keygen.py` (Python) and `key_derive.h` (C++) so that the same passphrase always produces the same key regardless of where it runs.

```
seed = FNV-1a 32-bit hash of the passphrase (ASCII bytes)

For each component (L, M, R rotors, reflector, starts, plugboard):
  LCG step:  seed = (seed × 1664525 + 1013904223) mod 2³²
  Fisher-Yates shuffle of [0..93] using LCG for random index selection

Rotor wirings:  three independent Fisher-Yates permutations of [0..93]
Reflector:      shuffle [0..93], pair consecutive entries into a swap table
Starting pos:   three LCG values mod 94
Plugboard:      shuffle [0..93], first 10 pairs become swap entries
```

The same implementation in two languages makes the passphrase path auditable: you can run `keygen.py` with any passphrase and compare the output tables against what `key_derive.h` would produce.
