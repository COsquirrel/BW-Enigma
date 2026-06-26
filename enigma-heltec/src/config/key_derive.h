// Copyright (C) 2025 Mike Barnett / Badger Works
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdint.h>
#include "config.h"

/* Deterministic key derivation from a shared passphrase.
   The algorithm is byte-for-byte identical to keygen.py so that a passphrase
   entered in the CrowPanel settings screen produces the same EnigmaConfig as
   running `python3 keygen.py "passphrase"` and flashing the result.

   Algorithm (in order):
     1. FNV-1a 32-bit hash of the passphrase
     2. LCG: seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
     3. Fisher-Yates shuffle of [0..93] → rotor_L, rotor_M, rotor_R
     4. Shuffle + pair-off → reflector (zero-fixed-point involution)
     5. Three LCG steps → rotor starting positions
     6. Shuffle + 10 pairs → plugboard swap table                     */

namespace KeyDerive {

static uint32_t _fnv1a(const char* s) {
    uint32_t h = 0x811C9DC5u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 0x01000193u;
    }
    return h;
}

static inline uint32_t _lcg(uint32_t seed) {
    return seed * 1664525u + 1013904223u;
}

/* Fisher-Yates shuffle of arr[0..CIPHER_RANGE-1], returns updated seed */
static uint32_t _shuffle(uint8_t arr[CIPHER_RANGE], uint32_t seed) {
    for (int i = CIPHER_RANGE - 1; i > 0; i--) {
        seed   = _lcg(seed);
        int j  = (int)(seed % (uint32_t)(i + 1));
        uint8_t tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
    return seed;
}

/* Derive a complete EnigmaConfig from passphrase.
   Overwrites ALL fields: rotor_wirings, reflector, rotor_start, plugboard. */
static void derive(const char* passphrase, EnigmaConfig& cfg) {
    uint32_t seed = _fnv1a(passphrase);

    /* Rotor wiring tables — three independent permutations */
    for (int r = 0; r < NUM_ROTORS; r++) {
        for (int i = 0; i < CIPHER_RANGE; i++) cfg.rotor_wirings[r][i] = (uint8_t)i;
        seed = _shuffle(cfg.rotor_wirings[r], seed);
    }

    /* Reflector: shuffle an index array, pair consecutive entries */
    uint8_t idx[CIPHER_RANGE];
    for (int i = 0; i < CIPHER_RANGE; i++) idx[i] = (uint8_t)i;
    seed = _shuffle(idx, seed);
    for (int k = 0; k < CIPHER_RANGE / 2; k++) {
        uint8_t a = idx[2 * k];
        uint8_t b = idx[2 * k + 1];
        cfg.reflector[a] = b;
        cfg.reflector[b] = a;
    }

    /* Rotor starting positions — one LCG step each */
    seed = _lcg(seed); cfg.rotor_start[0] = (uint8_t)(seed % CIPHER_RANGE);
    seed = _lcg(seed); cfg.rotor_start[1] = (uint8_t)(seed % CIPHER_RANGE);
    seed = _lcg(seed); cfg.rotor_start[2] = (uint8_t)(seed % CIPHER_RANGE);

    /* Plugboard — 10 swap pairs from another shuffle */
    for (int i = 0; i < CIPHER_RANGE; i++) { cfg.plugboard[i] = (uint8_t)i; idx[i] = (uint8_t)i; }
    seed = _shuffle(idx, seed);
    for (int k = 0; k < 10; k++) {
        uint8_t a = idx[2 * k];
        uint8_t b = idx[2 * k + 1];
        cfg.plugboard[a] = b;
        cfg.plugboard[b] = a;
    }
}

} // namespace KeyDerive
