#pragma once
#include <stdint.h>
#include <Arduino.h>
#include "rotor.h"
#include "plugboard.h"
#include "../config/config.h"

// Signal path per character:
//   plaintext (mapped to 0..94)
//     -> plugboard -> R forward -> M forward -> L forward
//     -> reflector
//     -> L reverse -> M reverse -> R reverse -> plugboard
//   ciphertext (mapped back to 32..126)
//
// Symmetric: same operation encrypts and decrypts.
// Both units must reset() to identical starting positions before each message.

class Enigma {
public:
    Enigma();
    void init(const EnigmaConfig& config);
    void reset();
    char processChar(char c);
    String processString(const String& input);
    void getRotorOffsets(uint8_t& l, uint8_t& m, uint8_t& r) const;

private:
    Rotor     _rotors[NUM_ROTORS];  // [0]=L [1]=M [2]=R
    Plugboard _plugboard;
    uint8_t   _reflector[CIPHER_RANGE];
    uint32_t  _char_count;
    void _advanceRotors();
};
