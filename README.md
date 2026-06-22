# BW Enigma

**Badger Works** | A hobbyist encrypted radio messenger inspired by the WWII Enigma machine.

> ⚠️ **This project is for fun and learning only. Not cryptographically secure. See disclaimer below.**

---

## What Is It

BW Enigma is a two-unit encrypted text messenger built on ESP32 hardware with no internet, no cell network, and no third-party infrastructure required. Messages are encrypted with a software Enigma-style rotor cipher, transmitted over ESP-NOW (or LoRa — selectable at compile time), and displayed on a touchscreen console-style chat interface.

The project started as a curiosity about how the historical Enigma machine worked and turned into a fully functional encrypted radio messenger built from scratch — cipher engine, radio layer, display, and UI all written by hand.

---

## Hardware

**Radio / cipher units (×2):**
- Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262 + SSD1306 OLED)
- Antenna matched to radio mode (2.4GHz for ESP-NOW, 915MHz for LoRa)

**UI host (×1 per station):**
- Elecrow CrowPanel 5" WZ8048C050 (ESP32-S3, 800×480 RGB touch display, GT911)
- Connected to Heltec via UART

---

## How It Works

```
┌─────────────────────┐          ┌──────────────────────┐
│  CrowPanel 5"       │          │  Heltec V3           │
│  LVGL console UI    │◄─UART──►│  Enigma cipher       │
│  Phosphor green     │  JSON    │  ESP-NOW / LoRa      │
│  touch keyboard     │          │  NVS key storage     │
└─────────────────────┘          └──────────┬───────────┘
                                            │ ESP-NOW / LoRa
                                            │
                                 ┌──────────┴───────────┐
                                 │  Heltec V3           │
                                 │  Enigma cipher       │
                                 │  ESP-NOW / LoRa      │
                                 │  NVS key storage     │
                                 └──────────┬───────────┘
                                            │
                                 ┌──────────┴───────────┐
                                 │  CrowPanel 5"        │
                                 │  LVGL console UI     │
                                 │  touch keyboard      │
                                 └──────────────────────┘
```

The CrowPanel handles all UI — touchscreen keyboard, phosphor green console chat, encryption toggle, signal strength and link diagnostics. The Heltec handles all RF and cipher work. Plaintext travels over a short UART cable between them. Ciphertext only ever touches the radio.

---

## UART Bridge Protocol

The CrowPanel and Heltec communicate over UART1 at 115200 baud using newline-delimited JSON.

| Direction | Type | Payload |
|---|---|---|
| CrowPanel → Heltec | `{"type":"tx","msg":"...","id":N}` | Send a message |
| CrowPanel → Heltec | `{"type":"status"}` | Request status |
| CrowPanel → Heltec | `{"type":"ping","seq":N}` | Link health ping |
| Heltec → CrowPanel | `{"type":"rx","plain":"...","cipher":"...","rssi":N}` | Received message |
| Heltec → CrowPanel | `{"type":"tx_ack","id":N,"stage":"uart\|radio\|delivered\|fail"}` | Delivery progress |
| Heltec → CrowPanel | `{"type":"remote_ack","id":N}` | Remote unit confirmed receipt |
| Heltec → CrowPanel | `{"type":"status","radio":"ok","key":"nvs","rssi":N}` | Status reply |

### Per-Message ACK Stages

Each sent message tracks delivery through 5 stages shown inline on the chat console:

```
----   PENDING    submitted to Heltec, no reply yet
>---   UART       Heltec received via UART
>>--   RADIO      esp_now_send() returned OK
>>>-   DELIVERED  remote MAC acknowledged at radio layer
>>>>   REMOTE     remote Heltec sent explicit application ACK
FAIL   FAILED     any stage failed
```

---

## ESP-NOW Radio (default)

In ESP-NOW mode the Heltec units use a binary packet framing over WiFi:

```
Data packet:  [0x01][id_lo][id_hi][ciphertext...]
ACK packet:   [0x02][id_lo][id_hi]
```

Each unit identifies its peer by WiFi MAC address (set in `config/config.h`). Role (UNIT1/UNIT2) is auto-detected at boot by comparing the chip's eFuse MAC against both configured MACs — no separate firmware builds required.

Switch to LoRa by changing `RADIO_MODE` in `config/config.h`:
```c
#define RADIO_MODE  RADIO_ESPNOW   // default
#define RADIO_MODE  RADIO_LORA     // SX1262 915 MHz
```

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

Rotor starting positions and plugboard configuration are stored in NVS and survive reboots. Rotor wiring tables are compiled in and shared between paired units as the secret key.

---

## Project Structure

```
BW-Enigma/
├── enigma-heltec/              ← Heltec radio/cipher firmware
│   ├── src/
│   │   ├── main.cpp            ← UART bridge, radio loop, ACK tracking
│   │   ├── utils.h             ← sanitizeInput, wordWrap
│   │   ├── cipher/
│   │   │   ├── rotor.h/.cpp
│   │   │   ├── plugboard.h/.cpp
│   │   │   └── enigma.h/.cpp
│   │   ├── radio/
│   │   │   ├── radio_interface.h
│   │   │   ├── espnow_radio.h  ← binary framing, TX callback, remote ACK
│   │   │   └── lora_radio.h
│   │   ├── display/
│   │   │   └── display.h       ← SSD1306 OLED screens + idle diagnostics
│   │   └── config/
│   │       ├── config.h        ← MACs, rotor tables, radio mode
│   │       ├── key_storage.h
│   │       ├── config_user.h        ← gitignored, your custom key
│   │       └── config_user.h.example
│   ├── enigma_test.py
│   └── platformio.ini
│
├── enigma-crowpanel/           ← CrowPanel UI firmware
│   ├── src/
│   │   ├── main.cpp            ← LVGL setup, callbacks, splash
│   │   ├── config.h            ← colors, layout, UART pins, ACK defines
│   │   ├── board_config.h      ← display/touch hardware init (CrowPanel only)
│   │   ├── ui/
│   │   │   ├── chat_screen.h   ← console UI, message pool, ACK labels
│   │   │   ├── keyboard.h      ← 3-layer touch keyboard
│   │   │   └── settings_screen.h
│   │   └── comms/
│   │       └── heltec_bridge.h ← UART JSON bridge, ping/pong, diagnostics
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
- PlatformIO with VS Code
- Python 3.x (for cipher test harness)
- 2× Heltec WiFi LoRa 32 V3
- 2× Elecrow CrowPanel 5" WZ8048C050

### Verify cipher first
```bash
cd enigma-heltec
python3 enigma_test.py       # full self test
python3 enigma_test.py -i    # interactive mode
```

### Configure unit MACs
Edit `enigma-heltec/src/config/config.h` with the WiFi MAC addresses of both Heltec units:
```c
#define UNIT1_MAC  {0xAC, 0xA7, 0x04, 0x3C, 0xC0, 0xDC}
#define UNIT2_MAC  {0xAC, 0xA7, 0x04, 0x3C, 0xC0, 0xE4}
```
Role (UNIT1 / UNIT2) is auto-detected at boot from the chip's eFuse MAC — the same firmware flashes to both units.

### Flash Heltec units (same firmware to both)
```bash
cd enigma-heltec
pio run -t upload
```

### Flash CrowPanel
```bash
cd enigma-crowpanel
pio run -t upload
```

### Custom key
```bash
cp enigma-heltec/src/config/config_user.h.example \
   enigma-heltec/src/config/config_user.h
# Fill in your own rotor wiring tables and starting positions
```

### UART wiring
```
Heltec GPIO1  (TX) ──►  CrowPanel GPIO44 (RX)
Heltec GPIO2  (RX) ◄──  CrowPanel GPIO43 (TX)
GND                ───  GND
```

---

## Features

- Enigma-style 3-rotor cipher, 94-character printable ASCII range
- Zero fixed points — even character range guarantees no character encrypts to itself
- Full plugboard and reflector implementation
- ESP-NOW radio link — no infrastructure, no internet, no pairing ceremony
- LoRa 915MHz radio layer — swap at compile time
- Binary ESP-NOW packet framing with message IDs and application-level ACK
- Per-message delivery tracking: UART → RADIO → DELIVERED → REMOTE
- NVS key storage — rotor starting positions persist across reboots
- Auto role detection from WiFi MAC — one firmware for both units
- Phosphor green console-style touch UI on CrowPanel 5"
- 3-layer onscreen keyboard (lower / upper / numeric+symbols)
- Encryption toggle — show ciphertext inline per message
- Callsign configurable per unit via settings screen
- Ping/pong link health with byte and JSON counters displayed on OLED
- UART recovery — CrowSerial reinit if silent for 20s
- Input sanitizer — strips non-printable characters
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
- [x] OLED display layer with idle diagnostics
- [x] ESP-NOW radio link with binary packet framing
- [x] LoRa radio layer
- [x] Two-way messaging
- [x] NVS key storage
- [x] Auto role detection from WiFi MAC
- [x] CrowPanel LVGL console UI (phosphor green, no bubbles)
- [x] UART JSON bridge with ping/pong diagnostics
- [x] Per-message ACK tracking (5 stages)
- [x] Settings screen / callsign config
- [x] Signal strength display
- [ ] Key generation utility
- [ ] Power management (OLED dim/sleep, CPU scaling)
- [ ] Make repo public

---

## Credits

Inspired by the historical Enigma machine and the codebreakers of Bletchley Park, particularly Alan Turing.

*A Badger Works project — COsquirrel*

---

## License

MIT — build it, learn from it, modify it. Just don't use it to protect anything real.
