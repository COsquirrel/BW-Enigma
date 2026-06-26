# BW Enigma

**Badger Works** | Encrypted LoRa radio messenger built on a two-board ESP32 architecture: Elecrow CrowPanel 5" touch console UI + Heltec WiFi LoRa 32 V3 cipher/radio unit. Inspired by the WWII Enigma machine. Licensed AGPL-3.0.

> ⚠️ **This project is for fun and learning only. Not cryptographically secure. See disclaimer below.**

---

## What Is It

BW Enigma is a two-unit encrypted text messenger built on ESP32 hardware with no internet, no cell network, and no third-party infrastructure required. Messages are encrypted with a software Enigma-style rotor cipher, transmitted over LoRa 915 MHz, and displayed on a touchscreen console-style chat interface.

The project started as a curiosity about how the historical Enigma machine worked and turned into a fully functional encrypted radio messenger built from scratch — cipher engine, radio layer, display, and UI all written by hand.

---

## Hardware

**Radio / cipher units (×2):**
- Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262 + SSD1306 OLED)
- 915 MHz LoRa antenna

**UI host (×1 per station):**
- Elecrow CrowPanel 5" WZ8048C050 (ESP32-S3, 800×480 RGB touch display, GT911)
- Connected to Heltec via UART

---

## How It Works

```
┌─────────────────────┐          ┌──────────────────────┐
│  CrowPanel 5"       │          │  Heltec V3           │
│  LVGL console UI    │◄─UART──►│  Enigma cipher       │
│  Phosphor green     │  JSON    │  LoRa SX1262         │
│  touch keyboard     │          │  NVS key + node ID   │
└─────────────────────┘          └──────────┬───────────┘
                                            │ LoRa 915 MHz
                                            │ (broadcast)
                                 ┌──────────┴───────────┐
                                 │  Heltec V3           │
                                 │  Enigma cipher       │
                                 │  LoRa SX1262         │
                                 │  NVS key + node ID   │
                                 └──────────┬───────────┘
                                            │
                                 ┌──────────┴───────────┐
                                 │  CrowPanel 5"        │
                                 │  LVGL console UI     │
                                 │  touch keyboard      │
                                 └──────────────────────┘
```

The CrowPanel handles all UI — touchscreen keyboard, phosphor green console chat, encryption toggle, and link health indicator. The Heltec handles all RF and cipher work. Plaintext travels over a short UART cable between them. Ciphertext only ever touches the radio.

All radios receive all traffic. Any unit with a matching cipher configuration can decrypt and read incoming messages. Units without matching configuration receive garbage.

---

## Identity and Provisioning

Each Heltec unit has a **node ID** — a short identifier stored in NVS. On first boot it defaults to the last 4 hex characters of the chip's WiFi MAC address (e.g. `C0DC`). The same firmware binary flashes to every unit — no per-unit code changes, no config files, no compile-time customization.

To set a custom node ID: open the CrowPanel settings screen, enter the new ID in the NODE ID field, and press SAVE. The CrowPanel sends it to the Heltec over UART, which writes it to NVS and uses it immediately. The value survives reboots.

---

## Packet Format

Every outgoing message is transmitted as a single encrypted LoRa packet:

```
plaintext block:  CALLSIGN|MESSAGE
                  ↓ Enigma encrypt (entire block as one string)
ciphertext:       [transmitted over LoRa]
```

The receiving unit decrypts the block, splits on the first `|` to recover callsign and message, and forwards them to its CrowPanel over UART. Because `|` encrypts along with the rest of the characters, a unit without the correct cipher sees only noise — no delimiter is detectable.

---

## UART Bridge Protocol

The CrowPanel and Heltec communicate over UART1 at 115200 baud using newline-delimited JSON.

| Direction | Type | Key fields |
|---|---|---|
| CrowPanel → Heltec | `tx` | `callsign`, `msg`, `id` |
| CrowPanel → Heltec | `status` | *(request status)* |
| CrowPanel → Heltec | `set_node_id` | `node_id` |
| CrowPanel → Heltec | `set_passphrase` | `passphrase` |
| Heltec → CrowPanel | `rx` | `callsign`, `plain`, `cipher`, `rssi`, `snr` |
| Heltec → CrowPanel | `tx_ack` | `id`, `stage` (`encrypted`\|`transmitted`\|`fail`), `cipher` |
| Heltec → CrowPanel | `status` | `radio`, `key`, `node_id` |
| Heltec → CrowPanel | `node_id_ack` | `node_id` |

The `tx_ack` with `stage: encrypted` carries the ciphertext so the CrowPanel can display it inline when the ENC toggle is on, before the message is transmitted.

### Message Pipeline

Each sent message shows a 4-character indicator that updates as it moves through the pipeline:

```
----   PENDING    submitted to Heltec, awaiting reply
>>>>   SENT       transmitted over LoRa
FAIL   FAILED     any stage failed
```

No delivery confirmation — LoRa is broadcast with no MAC-layer ACK. Message sent = done.

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

Three rotors advance on an odometer schedule — right rotor every character, middle every 94, left every 8836. The reflector is a true involution with zero fixed points, achieved by operating over 94 printable ASCII characters (32–125, tilde excluded for even range). Because the reflector makes the operation symmetric, the same function encrypts and decrypts. Both units reset to identical rotor starting positions before each message.

The entire `CALLSIGN|MESSAGE` block is encrypted as a single string — the delimiter encrypts with everything else, so an observer with the wrong key sees no structure.

Rotor starting positions and plugboard configuration are stored in NVS and survive reboots. Rotor wiring tables are compiled in and shared between paired units as the secret key.

---

## Project Structure

```
BW-Enigma/
├── enigma-heltec/              ← Heltec radio/cipher firmware
│   ├── src/
│   │   ├── main.cpp            ← UART bridge, LoRa RX/TX, node ID provisioning
│   │   ├── utils.h             ← sanitizeInput, wordWrap
│   │   ├── cipher/
│   │   │   ├── rotor.h/.cpp
│   │   │   ├── plugboard.h/.cpp
│   │   │   └── enigma.h/.cpp
│   │   ├── radio/
│   │   │   ├── radio_interface.h
│   │   │   └── lora_radio.h    ← SX1262 RadioLib, ISR-driven RX, blocking TX
│   │   ├── display/
│   │   │   └── display.h       ← SSD1306 OLED: splash, sent, received screens
│   │   └── config/
│   │       ├── config.h        ← rotor tables, NVS keys, OLED pins
│   │       ├── key_storage.h   ← NVS load/save for rotor start, plugboard, passphrase
│   │       ├── key_derive.h    ← passphrase → EnigmaConfig derivation (matches keygen.py)
│   │       ├── config_user.h        ← gitignored — your custom key
│   │       └── config_user.h.example
│   ├── keygen.py               ← key generator: passphrase or random tables → config_user.h
│   ├── enigma_test.py
│   └── platformio.ini
│
├── enigma-crowpanel/           ← CrowPanel UI firmware
│   ├── src/
│   │   ├── main.cpp            ← LVGL setup, callbacks, splash
│   │   ├── config.h            ← colors, layout, UART pins, pipeline stage defines
│   │   ├── board_config.h      ← display/touch hardware init (CrowPanel specific)
│   │   ├── ui/
│   │   │   ├── chat_screen.h   ← console UI, message ring buffer, pipeline labels
│   │   │   ├── keyboard.h      ← 3-layer touch keyboard (lower/upper/symbols)
│   │   │   └── settings_screen.h ← callsign, node ID, and key passphrase settings
│   │   └── comms/
│   │       └── heltec_bridge.h ← UART JSON bridge, FreeRTOS Core 0/1 split
│   ├── include/
│   │   └── lv_conf.h
│   ├── partitions.csv
│   └── platformio.ini
│
└── README.md
```

---

## Getting Started

### Requirements
- PlatformIO (VS Code extension or CLI)
- Python 3.x (for cipher test harness)
- 2× Heltec WiFi LoRa 32 V3
- 2× Elecrow CrowPanel 5" WZ8048C050

### Verify the cipher
```bash
cd enigma-heltec
python3 enigma_test.py       # full self-test
python3 enigma_test.py -i    # interactive mode
```

### Flash Heltec units (identical firmware to both)
```bash
cd enigma-heltec
pio run -t upload
```

Node IDs are provisioned at runtime — no per-unit changes before flashing.

### Flash CrowPanel units (identical firmware to both)
```bash
cd enigma-crowpanel
pio run -t upload
```

### Set callsign and node ID
Open settings (gear icon) on each CrowPanel. Enter the unit's callsign and a node ID, then press SAVE. The node ID is sent to the Heltec and written to NVS — it persists across reboots.

### Set a private key

**Option A — settings screen (no reflash):** Enter the same passphrase in the KEY field on both CrowPanels and press SAVE. The Heltec derives a full cipher key from the passphrase at runtime.

**Option B — keygen (compile-time):**
```bash
cd enigma-heltec
python3 keygen.py                        # auto-generated passphrase
python3 keygen.py "my secret phrase"     # derive from your own passphrase
python3 keygen.py --random               # pure random tables (max entropy)
# pio run -t upload on both Heltec units from the same build
```

See `KEY_SETUP.md` for fingerprint verification, the derivation algorithm, and security notes.

### UART wiring
```
Heltec GPIO1  (TX) ──►  CrowPanel GPIO44 (RX)
Heltec GPIO2  (RX) ◄──  CrowPanel GPIO43 (TX)
GND                ───  GND
```

---

## Features

- Enigma-style 3-rotor cipher over 94-character printable ASCII range
- Zero fixed points — even character range guarantees no character encrypts to itself
- Entire `CALLSIGN|MESSAGE` block encrypted as one unit — no structure visible without the key
- LoRa 915 MHz broadcast — no infrastructure, no pairing, no addressing
- Single firmware binary for all units — identity is fully runtime-provisioned via NVS
- Node ID defaults to last 4 hex chars of WiFi MAC; user-settable via settings screen
- NVS key storage — rotor starting positions and node ID persist across reboots
- Phosphor green console-style touch UI on CrowPanel 5"
- 3-layer onscreen keyboard (lower / upper / numeric+symbols)
- Encryption toggle — show ciphertext inline per message
- Per-message pipeline indicator (`----` → `>>>>` → `FAIL`)
- Callsign, node ID, and cipher key passphrase configurable via settings screen
- `keygen.py` generates compile-time keys from a passphrase or random tables; fingerprint output for cross-unit verification
- Link health dot — turns red if Heltec goes quiet for 30 seconds
- UART recovery — CrowSerial reinit if line is silent for 20 s
- Input sanitizer — strips non-cipher-range characters before encryption
- Python test harness for desktop cipher verification

---

## ⚠️ Security Disclaimer

**This is not secure encryption. Do not use this to protect anything sensitive.**

BW Enigma is an educational hobbyist project inspired by the historical Enigma machine. It is intentionally designed for learning and experimentation.

**Known weaknesses:**
- The rotor wiring tables are the entire secret key — anyone with the compiled firmware can decrypt all messages
- Small key space compared to modern standards like AES-256
- Vulnerable to known-plaintext attacks
- No message authentication — packets can be tampered with undetected
- No forward secrecy — one key compromise exposes all past and future messages
- Static key per flash cycle

The historical Enigma was broken at Bletchley Park not by brute force but by exploiting structural weaknesses and operator errors. This implementation has similar and additional weaknesses. It is a toy cipher, not a security tool.

For real secure communications use Signal, AES-256, or other peer-reviewed cryptographic protocols.

---

## Why Build This

The Enigma machine is one of the most historically significant cryptographic devices ever built. Alan Turing's work breaking it at Bletchley Park helped shorten WWII and laid the foundation for modern computing. Understanding how it worked — and why it failed — is a great way to learn symmetric encryption, substitution ciphers, and key management fundamentals.

Building it on real hardware with a radio link and a proper touch UI makes the concept tangible in a way that reading about it never quite does. Plus it's just a genuinely fun thing to build.

---

## Roadmap

- [x] Cipher engine — rotor, plugboard, reflector, zero fixed points
- [x] Input sanitizer and word wrap
- [x] Python test harness
- [x] OLED display layer (splash, sent, received screens)
- [x] LoRa 915 MHz radio via RadioLib SX1262
- [x] Single firmware binary — runtime identity provisioning via NVS
- [x] Node ID from WiFi MAC with user-settable NVS override
- [x] Encrypted broadcast packet — CALLSIGN|MESSAGE block
- [x] Two-way messaging
- [x] NVS key storage
- [x] CrowPanel LVGL console UI (phosphor green, console style)
- [x] UART JSON bridge with FreeRTOS Core 0/1 split
- [x] Per-message pipeline indicator
- [x] Callsign + node ID settings screen
- [x] Self-rx fix — TX-done ISR no longer triggers false receive
- [x] Key generation utility (`keygen.py`) with passphrase derivation and runtime settings screen entry
- [ ] Power management (OLED dim/sleep, CPU frequency scaling)
- [ ] Hardware kit interest check (PCB, enclosure, assembled unit)
- [ ] GitHub Sponsors / support Badger Works

---

## Credits

Inspired by the historical Enigma machine and the codebreakers of Bletchley Park, particularly Alan Turing.

*A Badger Works project — COsquirrel*

Commercial licensing inquiries: [help@BadgerWorksIO.com] 

---

## License

Copyright © 2025 Badger Works (github.com/COsquirrel)
This project is licensed under the GNU Affero General Public License v3.0.
See the LICENSE file for details.
Personal and hobbyist use is welcome. Commercial use requires written permission.
