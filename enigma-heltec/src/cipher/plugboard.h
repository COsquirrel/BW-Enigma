// Copyright (C) 2025 Mike Barnett / Badger Works
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <stdint.h>
#include "../config/config.h"

class Plugboard {
public:
    Plugboard();
    void init(const uint8_t* table);
    uint8_t apply(uint8_t in) const;
private:
    uint8_t _table[CIPHER_RANGE];
};
