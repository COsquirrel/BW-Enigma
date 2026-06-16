# BW Enigma

**Badger Works** | A hobbyist encrypted LoRa messenger inspired by the WWII Enigma machine.

> ⚠️ **This project is for fun and learning only. Not cryptographically secure. See disclaimer below.**

---

## What Is It

BW Enigma is a two unit encrypted text messenger built on ESP32 LoRa hardware with no internet, no cell network, and no third party infrastructure required. Messages are encrypted with a software Enigma-style rotor cipher, transmitted over LoRa RF, and displayed on a touchscreen IRC-style chat interface.

The project started as a curiosity about how the historical Enigma machine worked and turned into a fully functional encrypted radio messenger built from scratch — cipher engine, radio layer, display, and UI all written by hand.

---

## Hardware

**Radio units (x2):**
- Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262 LoRa + SSD1306 OLED)
- 915MHz LoRa antenna

**UI host (x1 per station):**
- Elecrow CrowPanel 5" ESP32-S3 (800x480 touch display)
- Connected to Heltec via UART

---

## How It Works

```
┌─────────────────────┐          ┌──────────────────────┐
│  CrowPanel 5"       │          │  Heltec V3           │
│  LVGL touch UI      │◄─UART──►│  Enigma cipher       │
│  IRC chat interface │  JSON    │  SX1262 LoRa radio   │
│  On screen keyboard │          │  NVS key storage     │
└─────────────────────┘          └──────────┬───────────┘
                                            │ LoRa 915MHz
                                            │
                                 ┌──────────┴───────────┐
                                 │  Heltec V3           │
                                 │  Enigma cipher       │
                                 │  SX1262 LoRa radio   │
                                 │  NVS key storage     │
                                 └──────────┬───────────┘
                                            │
                                 ┌──────────┴───────────┐
                                 │  CrowPanel 5"        │
                                 │  LVGL touch UI       │
                                 │  IRC chat interface  │
                                 │  On screen keyboard  │
                                 └─────────────────────-┘
```

The CrowPanel handles all UI — touchscreen keyboard, IRC style chat bubbles, encryption toggle, signal strength display. The Heltec handles all RF and cipher work. Plaintext travels over a short UART cable between them. Ciphertext only ever touches the radio.

---

## Cipher

Each character passes through this signal path:

```
plaintext
  → plugboard
  → Rotor R (forward) → Rotor M (forward) → Rotor L (forward)
  → reflector
  → Rotor L (reverse) → Rotor M (reverse) → Rotor R (reverse)
  → plugboard
  → ciphertext
```

Three rotors advance on an odometer schedule — right rotor every character, middle every 94, left every 8836. The reflector is a true involution with zero fixed points, achieved by operating over 94 printable ASCII characters (32-125, tilde excluded for even range). Because the reflector makes the operation symmetric, the same function encrypts and decrypts. Both units reset to identical rotor starting positions before each message.

Rotor starting positions and plugboard configuration are stored in NVS and survive reboots. Rotor wiring tables are compiled in and shared between paired units as the secret key.

---

## Project Structure

```
BW-Enigma/
├── enigma-heltec/          ← Heltec radio/cipher firmware
│   ├── src/
│   │   ├── main.cpp
│   │   ├── utils.h
│   │   ├── cipher/
│   │   │   ├── rotor.h/.cpp
│   │   │   ├── plugboard.h/.cpp
│   │   │   └── enigma.h/.cpp
│   │   ├── radio/
│   │   │   ├── radio_interface.h
│   │   │   ├── espnow_radio.h
│   │   │   └── lora_radio.h
│   │   ├── display/
│   │   │   └── display.h
│   │   └── config/
│   │       ├── config.h
│   │       ├── key_storage.h
│   │       ├── config_user.h        ← gitignored
│   │       └── config_user.h.example
│   ├── enigma_test.py
│   └── platformio.ini
│
├── enigma-crowpanel/       ← CrowPanel UI firmware
│   ├── src/
│   │   ├── main.cpp
│   │   ├── ui/
│   │   │   ├── chat_screen.h
│   │   │   ├── keyboard.h
│   │   │   └── settings_screen.h
│   │   └── comms/
│   │       └── heltec_bridge.h
│   └── platformio.ini
│
└── README.md
```

---

## Getting Started

### Requirements
- PlatformIO with VS Code
- Python 3.x (for cipher test harness)
- 2x Heltec WiFi LoRa 32 V3
- 2x Elecrow CrowPanel 5" ESP32-S3 (optional — standalone OLED mode works without)

### Verify cipher first
```bash
cd enigma-heltec
python3 enigma_test.py       # full self test
python3 enigma_test.py -i    # interactive mode
```

### Flash Heltec units
```bash
cd enigma-heltec

# Unit 1 - set UNIT_ROLE ROLE_SENDER in config/config.h
pio run -t upload

# Unit 2 - set UNIT_ROLE ROLE_RECEIVER in config/config.h
pio run -t upload
```

### Custom key
```bash
cp src/config/config_user.h.example src/config/config_user.h
# Fill in your own rotor tables
# python3 generate_key.py  <- coming soon
```

### UART wiring (CrowPanel to Heltec)
```
CrowPanel TX  →  Heltec RX (GPIO 44)
CrowPanel RX  →  Heltec TX (GPIO 43)
GND           →  GND
```

---

## Features

- Enigma-style 3-rotor cipher, 94 character printable ASCII range
- Zero fixed points — even character range guarantees no character encrypts to itself
- Full plugboard and reflector implementation  
- LoRa 915MHz radio link — no infrastructure required
- ESP-NOW fallback for short range testing
- NVS key storage — rotor starting positions persist across reboots
- IRC-style touch chat UI on CrowPanel 5" with onscreen keyboard
- Encryption toggle — see ciphertext inline per message
- Callsign configurable per unit (BDGR1/BDGR2)
- Input sanitizer — strips non-printable characters
- Word wrap for OLED and chat display
- Python test harness for desktop cipher verification
- Abstract radio layer — swap LoRa/ESP-NOW at compile time

---

## ⚠️ Security Disclaimer

**This is not secure encryption. Do not use this to protect anything sensitive.**

BW Enigma is an educational hobbyist project inspired by the historical Enigma machine. It is intentionally designed for learning and experimentation.

**Known weaknesses:**
- The rotor wiring tables are the entire secret key — anyone with the compiled firmware can decrypt all messages
- Small key space compared to modern standards like AES-256
- Vulnerable to known plaintext attacks
- No message authentication — packets can be tampered with undetected
- No forward secrecy — one key compromise exposes all past and future messages
- Static key per flash cycle

The historical Enigma was broken at Bletchley Park not by brute force but by exploiting structural weaknesses and operator errors. This implementation has similar and additional weaknesses. It is a toy cipher, not a security tool.

For real secure communications use Signal, AES-256, or other peer reviewed cryptographic protocols.

---

## Why Build This

The Enigma machine is one of the most historically significant cryptographic devices ever built. Alan Turing's work breaking it at Bletchley Park helped shorten WWII and laid the foundation for modern computing. Understanding how it worked — and why it failed — is a great way to learn symmetric encryption, substitution ciphers, and key management fundamentals.

Building it on real hardware with a LoRa radio link and a proper touch UI makes the concept tangible in a way that reading about it never quite does. Plus it's just a genuinely fun thing to build.

---

## Roadmap

- [x] Cipher engine — rotor, plugboard, reflector, zero fixed points
- [x] Input sanitizer and word wrap
- [x] Python test harness
- [x] OLED display layer
- [x] ESP-NOW radio link
- [x] Two way messaging
- [x] NVS key storage
- [x] LoRa radio layer
- [ ] CrowPanel LVGL chat UI
- [ ] UART JSON bridge
- [ ] Key generation utility
- [ ] Settings screen / callsign config
- [ ] Signal strength display
- [ ] Make repo public

---

## Credits

Inspired by the historical Enigma machine and the codebreakers of Bletchley Park, particularly Alan Turing.

*A Badger Works project — COsquirrel*

---

## License

MIT — build it, learn from it, modify it. Just don't use it to protect anything real.
