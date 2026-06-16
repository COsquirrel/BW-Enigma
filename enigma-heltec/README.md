# Badger Works Enigma Messenger

A hobbyist implementation of an Enigma-style rotor cipher running on ESP32 LoRa hardware. Two units communicate over LoRa radio with messages encrypted using a software rotor cipher inspired by the WWII Enigma machine.

**This project is for fun and learning only. See the security disclaimer below.**

---

## What It Is

A pair of ESP32 LoRa nodes that exchange short encrypted text messages over RF with no internet, no cell network, and no third party infrastructure required. Messages are encrypted with a multi-rotor substitution cipher operating over printable ASCII characters 32-125 (94 characters), displayed on onboard OLED screens, and entered via serial console.

Inspired by the historical Enigma machine but extended to modern hardware and the full ASCII printable range rather than just A-Z.

---

## Hardware

- 2x **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262 + SSD1306 OLED)
- USB-C cable for flashing and serial console
- Optional: 915MHz antenna upgrade for extended range
- Future: LoRa long range mode replacing ESP-NOW

---

## Features

- Enigma-style 3-rotor cipher over printable ASCII (32-126, 95 characters)
- Full plugboard and reflector implementation
- Input sanitizer — strips non-printable characters, collapses whitespace
- Word wrap on receive — fits decoded message to OLED display width
- Abstracted radio layer — swap ESP-NOW for LoRa without touching cipher code
- Hardcoded test key for development with user key override support
- Python test harness for validating cipher logic before flashing

---

## Project Structure

```
enigma-esp32/
├── src/
│   ├── main.cpp
│   ├── utils.h                  # sanitizer and word wrap
│   ├── cipher/
│   │   ├── rotor.h / .cpp       # single rotor with forward/reverse
│   │   ├── plugboard.h / .cpp   # symmetric swap table
│   │   └── enigma.h / .cpp      # full cipher engine
│   ├── radio/
│   │   └── radio_interface.h    # abstract radio layer
│   ├── display/                 # OLED layer (coming)
│   └── config/
│       ├── config.h             # default test key - see disclaimer
│       ├── config_user.h        # YOUR key goes here - gitignored
│       └── config_user.h.example
├── enigma_test.py               # desktop cipher verification
└── platformio.ini
```

---

## Getting Started

### Requirements
- [PlatformIO](https://platformio.org/) with VS Code or CLI
- Python 3.x for the test harness
- Two Heltec WiFi LoRa 32 V3 boards

### Verify cipher logic first
Before touching hardware run the Python test harness on your desktop:
```bash
python3 enigma_test.py          # run all self tests
python3 enigma_test.py -i       # interactive mode
python3 enigma_test.py "hello"  # encrypt a specific message
```
All tests should pass before flashing.

### Flash
```bash
# Clone the repo
git clone https://github.com/COsquirrel/enigma-esp32.git
cd enigma-esp32

# Build and flash sender unit
pio run -e heltec_lora_v3 -t upload

# Set UNIT_ROLE to ROLE_RECEIVER in config.h for second unit
# then flash second unit
```

### Custom key (recommended)
```bash
cp src/config/config_user.h.example src/config/config_user.h
# Edit config_user.h with your own rotor tables
# Generate fresh tables with:
python3 generate_key.py
```

---

## How The Cipher Works

Each character passes through this signal path:

```
plaintext char
  → plugboard substitution
  → Rotor R (forward)
  → Rotor M (forward)  
  → Rotor L (forward)
  → reflector
  → Rotor L (reverse)
  → Rotor M (reverse)
  → Rotor R (reverse)
  → plugboard substitution
  → ciphertext char
```

After each character the rightmost rotor advances one position. After every 95 characters the middle rotor advances. After every 9025 characters the left rotor advances — identical in principle to the odometer-style advance of the original Enigma machine.

Because the reflector makes the operation symmetric, encryption and decryption are the same function. Both units reset to identical rotor starting positions before each message.

The cipher operates entirely within printable ASCII 32-125 (94 characters). Tilde `~` (126) is intentionally excluded to give an even character count. Input is mapped to 0-93, processed, mapped back to 32-125. No clamping, no out-of-range values.

Using an even range (94) means the reflector can pair every character with a different character — mathematically guaranteeing zero fixed points. No character will ever encrypt to itself at any rotor position.

---

## ⚠️ Security Disclaimer

**This is not secure encryption. Do not use this to protect anything sensitive.**

This project is an educational hobbyist implementation of a rotor cipher inspired by the historical Enigma machine. It is intentionally designed for learning and experimentation, not security.

**Known weaknesses:**

- **The key is the rotor tables** — anyone with access to `config.h` or `config_user.h` can decrypt all messages instantly
- **Small key space** — nowhere near the complexity of modern cryptographic standards like AES-256
- **Known plaintext attacks** — if an attacker knows or can guess part of a plaintext message they can use it to narrow down the cipher state significantly
- **No message authentication** — packets can be tampered with in transit with no detection
- **Small character set** — tilde `~` cannot be sent as it is excluded from the cipher range to eliminate reflector fixed points
- **Static key per flash** — there is no forward secrecy; compromising the key compromises all past and future messages

**For real secure communications** use established, peer reviewed cryptographic protocols. [Signal Protocol](https://signal.org/docs/) and AES-256 exist for good reason.

The historical Enigma machine was eventually broken by Allied codebreakers at Bletchley Park — not by brute force but by exploiting structural weaknesses and operator errors. This implementation has similar and additional weaknesses. It is a toy cipher, not a security tool.

---

## Why Build This?

The Enigma machine is one of the most historically significant cryptographic devices ever built. Understanding how it worked — and why it failed — is a great way to learn the fundamentals of symmetric encryption, substitution ciphers, and the importance of key management.

Building it on real hardware with a radio link makes the concept tangible in a way that reading about it never quite does.

---

## Roadmap

- [x] Cipher engine — rotor, plugboard, reflector
- [x] Input sanitizer
- [x] Word wrap for OLED display
- [x] Python test harness
- [ ] OLED display layer
- [ ] ESP-NOW radio link
- [ ] Two way messaging
- [ ] NVS key storage
- [ ] LoRa radio layer swap
- [ ] Key generation utility
- [ ] Config menu on device

---

## Credits

Inspired by the historical Enigma machine and the work of the codebreakers at Bletchley Park, particularly Alan Turing whose work breaking Enigma helped shorten WWII and laid the foundation for modern computing.

---

## License

MIT — do whatever you want with it, just don't use it to protect anything real.

*A Badger Works project.*
