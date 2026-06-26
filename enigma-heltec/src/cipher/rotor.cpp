// Copyright (C) 2025 Mike Barnett / Badger Works
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rotor.h"

Rotor::Rotor() : _offset(0), _start_offset(0) {
    for (int i = 0; i < CIPHER_RANGE; i++) {
        _wiring[i] = i;
        _inverse[i] = i;
    }
}

void Rotor::init(const uint8_t* wiring, uint8_t start_offset) {
    _start_offset = start_offset % CIPHER_RANGE;
    _offset = _start_offset;
    for (int i = 0; i < CIPHER_RANGE; i++) {
        _wiring[i] = wiring[i] % CIPHER_RANGE;
    }
    for (int i = 0; i < CIPHER_RANGE; i++) {
        _inverse[_wiring[i]] = i;
    }
}

void Rotor::advance() {
    _offset = (_offset + 1) % CIPHER_RANGE;
}

void Rotor::reset() {
    _offset = _start_offset;
}

uint8_t Rotor::forward(uint8_t in) const {
    uint8_t shifted = (in + _offset) % CIPHER_RANGE;
    uint8_t subst   = _wiring[shifted];
    uint8_t out     = (subst - _offset + CIPHER_RANGE) % CIPHER_RANGE;
    return out;
}

uint8_t Rotor::reverse(uint8_t in) const {
    uint8_t shifted = (in + _offset) % CIPHER_RANGE;
    uint8_t subst   = _inverse[shifted];
    uint8_t out     = (subst - _offset + CIPHER_RANGE) % CIPHER_RANGE;
    return out;
}
