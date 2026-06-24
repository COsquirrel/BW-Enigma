#pragma once
#include <Preferences.h>
#include "config.h"

// Persists the user-configurable key material (rotor starting positions and
// plugboard) in NVS under the "enigma" namespace.  Rotor wirings and the
// reflector stay hardcoded in config.h.
//
// Both units must have identical values to communicate correctly.

class KeyStorage {
public:
    // Returns true and populates arrays if saved values exist, false otherwise.
    bool load(uint8_t rotor_start[NUM_ROTORS], uint8_t plugboard[CIPHER_RANGE]) {
        Preferences prefs;
        prefs.begin("enigma", /*readOnly=*/true);
        bool ok = prefs.isKey("rotor_start") && prefs.isKey("plugboard");
        if (ok) {
            prefs.getBytes("rotor_start", rotor_start, NUM_ROTORS);
            prefs.getBytes("plugboard",   plugboard,   CIPHER_RANGE);
        }
        prefs.end();
        return ok;
    }

    // Returns true if both arrays were written successfully.
    bool save(const uint8_t rotor_start[NUM_ROTORS], const uint8_t plugboard[CIPHER_RANGE]) {
        Preferences prefs;
        prefs.begin("enigma", /*readOnly=*/false);
        bool ok = prefs.putBytes("rotor_start", rotor_start, NUM_ROTORS)   == NUM_ROTORS
               && prefs.putBytes("plugboard",   plugboard,   CIPHER_RANGE) == CIPHER_RANGE;
        prefs.end();
        return ok;
    }

    // Wipes the entire "enigma" NVS namespace.
    void clear() {
        Preferences prefs;
        prefs.begin("enigma", /*readOnly=*/false);
        prefs.clear();
        prefs.end();
    }

    // Passphrase-based key derivation — stored separately from rotor_start/plugboard.
    // Returns true and fills buf if a passphrase is saved; false otherwise.
    bool loadPassphrase(char* buf, size_t len) {
        Preferences prefs;
        prefs.begin("enigma", /*readOnly=*/true);
        bool ok = prefs.isKey("passphrase");
        if (ok) {
            String s = prefs.getString("passphrase", "");
            strncpy(buf, s.c_str(), len - 1);
            buf[len - 1] = '\0';
            ok = buf[0] != '\0';
        }
        prefs.end();
        return ok;
    }

    bool savePassphrase(const char* phrase) {
        Preferences prefs;
        prefs.begin("enigma", /*readOnly=*/false);
        bool ok = prefs.putString("passphrase", phrase) > 0;
        prefs.end();
        return ok;
    }

    void clearPassphrase() {
        Preferences prefs;
        prefs.begin("enigma", /*readOnly=*/false);
        prefs.remove("passphrase");
        prefs.end();
    }

};
