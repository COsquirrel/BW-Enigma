#include "enigma.h"

Enigma::Enigma() : _char_count(0) {
    for (int i = 0; i < CIPHER_RANGE; i++) _reflector[i] = i;
}

void Enigma::init(const EnigmaConfig& config) {
    _rotors[0].init(config.rotor_wirings[0], config.rotor_start[0]);
    _rotors[1].init(config.rotor_wirings[1], config.rotor_start[1]);
    _rotors[2].init(config.rotor_wirings[2], config.rotor_start[2]);
    _plugboard.init(config.plugboard);
    for (int i = 0; i < CIPHER_RANGE; i++) {
        _reflector[i] = config.reflector[i] % CIPHER_RANGE;
    }
    _char_count = 0;
}

void Enigma::reset() {
    _rotors[0].reset();
    _rotors[1].reset();
    _rotors[2].reset();
    _char_count = 0;
}

void Enigma::_advanceRotors() {
    _rotors[2].advance();
    if ((_char_count % ROTOR_PERIOD_M) == 0) _rotors[1].advance();
    if ((_char_count % ROTOR_PERIOD_L) == 0) _rotors[0].advance();
    _char_count++;
}

char Enigma::processChar(char c) {
    // Map printable ASCII to 0..94
    uint8_t val = ((uint8_t)c - PRINTABLE_START) % CIPHER_RANGE;

    val = _plugboard.apply(val);

    val = _rotors[2].forward(val);
    val = _rotors[1].forward(val);
    val = _rotors[0].forward(val);

    val = _reflector[val];

    val = _rotors[0].reverse(val);
    val = _rotors[1].reverse(val);
    val = _rotors[2].reverse(val);

    val = _plugboard.apply(val);

    _advanceRotors();

    // Map back to printable ASCII 32..126
    return (char)(val + PRINTABLE_START);
}

String Enigma::processString(const String& input) {
    reset();
    String output = "";
    output.reserve(input.length());
    for (int i = 0; i < (int)input.length(); i++) {
        output += processChar(input[i]);
    }
    return output;
}

void Enigma::getRotorOffsets(uint8_t& l, uint8_t& m, uint8_t& r) const {
    l = _rotors[0].offset();
    m = _rotors[1].offset();
    r = _rotors[2].offset();
}
