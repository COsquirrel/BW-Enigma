// Copyright (C) 2025 Mike Barnett / Badger Works
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "plugboard.h"

Plugboard::Plugboard() {
    for (int i = 0; i < CIPHER_RANGE; i++) _table[i] = i;
}

void Plugboard::init(const uint8_t* table) {
    for (int i = 0; i < CIPHER_RANGE; i++) _table[i] = table[i] % CIPHER_RANGE;
}

uint8_t Plugboard::apply(uint8_t in) const {
    return _table[in % CIPHER_RANGE];
}
