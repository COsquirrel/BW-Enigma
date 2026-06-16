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
