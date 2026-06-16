#pragma once
#include <stdint.h>
#include "../config/config.h"

class Rotor {
public:
    Rotor();
    void init(const uint8_t* wiring, uint8_t start_offset);
    void advance();
    void reset();
    uint8_t forward(uint8_t in) const;
    uint8_t reverse(uint8_t in) const;
    uint8_t offset() const { return _offset; }

private:
    uint8_t _wiring[CIPHER_RANGE];
    uint8_t _inverse[CIPHER_RANGE];
    uint8_t _offset;
    uint8_t _start_offset;
};
