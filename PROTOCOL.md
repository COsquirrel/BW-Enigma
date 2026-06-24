# BW Enigma — Protocol Specification

This document defines the complete wire protocol for BW Enigma: the UART JSON bridge between the CrowPanel and Heltec, the LoRa packet format, the cipher specification, and the message pipeline state machine. It is intended for anyone implementing a compatible unit or extending the system.

---

## Architecture Overview

Each BW Enigma station is a two-board system:

```
┌─────────────────────┐                    ┌─────────────────────┐
│   CrowPanel 5"      │                    │   Heltec V3         │
│   (UI host)         │◄── UART1, JSON ───►│   (cipher + radio)  │
│   Core 1: LVGL      │    115200 baud     │   Core 0: UART RX   │
│   Core 0: UART RX   │    newline-delim.  │   Core 1: LoRa, enc │
└─────────────────────┘                    └─────────┬───────────┘
                                                     │
                                              LoRa 915 MHz
                                             (broadcast, no ACK)
                                                     │
                                           ┌─────────┴───────────┐
                                           │   Remote station    │
                                           │   Heltec V3 + Panel │
                                           └─────────────────────┘
```

**Division of responsibility:**

| Concern | CrowPanel | Heltec |
|---|---|---|
| UI and touch input | ✓ | |
| UART bridge (JSON) | ✓ | ✓ |
| Enigma cipher | | ✓ |
| LoRa RF | | ✓ |
| NVS (key, node ID) | callsign only | rotor start, plugboard, node ID |
| Node identity | stores last-known node ID | authoritative |

---

## UART Bridge

### Physical layer

| Parameter | Value |
|---|---|
| Interface | UART1 on Heltec, UART2 (`Serial2`) on CrowPanel |
| Baud rate | 115200 |
| Frame | 8N1 |
| Heltec TX | GPIO1 |
| Heltec RX | GPIO2 |
| CrowPanel TX | GPIO43 |
| CrowPanel RX | GPIO44 |
| Logic level | 3.3 V |

### Frame format

Each message is a single JSON object serialized to one line, terminated with `\n`. No framing bytes, no length prefix, no CRC. The receiver accumulates characters until `\n`, then parses the complete object.

```
{"type":"tx","callsign":"BDGR1","msg":"hello","id":7}\n
```

**Rules:**
- Lines begin with `{`. Any character received before the first `{` on a new line is discarded.
- A new `{` while a line is in progress resets the accumulator (handles partial/corrupt lines).
- Lines exceeding 511 bytes are discarded and the accumulator resets.
- A line in progress that receives no new characters for 2000 ms is discarded (timeout).
- The Heltec reinitializes its UART if the line is silent for 20 seconds (recovery path).

---

## Message Types

### CrowPanel → Heltec

#### `tx` — Transmit a message

Requests the Heltec to encrypt and transmit a message over LoRa.

```json
{
  "type": "tx",
  "callsign": "BDGR1",
  "msg": "hello world",
  "id": 7
}
```

| Field | Type | Description |
|---|---|---|
| `callsign` | string | Sender identifier prepended to the plaintext block. Max ~15 chars practical. |
| `msg` | string | Plaintext message. Characters outside 32–125 ASCII are stripped before encryption. |
| `id` | uint16 | Caller-assigned message ID used to correlate `tx_ack` responses. |

The Heltec will respond with two `tx_ack` messages: one when encryption is complete (`encrypted`), one when the LoRa transmission completes (`transmitted`) or fails (`fail`).

#### `status` — Request status

Requests a `status` response from the Heltec. The CrowPanel sends this on startup and re-sends it if the Heltec has been silent for 30 seconds.

```json
{"type": "status"}
```

No additional fields.

#### `set_node_id` — Set node identifier

Instructs the Heltec to update its node ID in NVS. The new value takes effect immediately and persists across reboots.

```json
{
  "type": "set_node_id",
  "node_id": "C0DC"
}
```

| Field | Type | Description |
|---|---|---|
| `node_id` | string | 1–8 characters. No validation of content beyond length. |

---

### Heltec → CrowPanel

#### `rx` — Incoming LoRa message

Sent whenever the Heltec successfully decrypts an incoming LoRa packet and parses a valid `CALLSIGN|MESSAGE` structure from the plaintext.

```json
{
  "type": "rx",
  "callsign": "UNIT2",
  "plain": "hello from the other side",
  "cipher": "Xm4.tQ>aL!z...",
  "rssi": -87,
  "snr": 7.25
}
```

| Field | Type | Description |
|---|---|---|
| `callsign` | string | Extracted from the decrypted block (before the `\|`). |
| `plain` | string | Decrypted message text (after the `\|`). |
| `cipher` | string | Raw ciphertext as received over LoRa. |
| `rssi` | int | Received signal strength in dBm. |
| `snr` | float | Signal-to-noise ratio in dB. |

Packets that decrypt to a string with no `|` character are silently discarded — this is the normal behavior when a packet is received from a unit with a different cipher configuration.

#### `tx_ack` — Transmit acknowledgment

Sent in response to a `tx` command. Delivered twice per successful transmission: once after encryption, once after the LoRa send completes.

```json
{
  "type": "tx_ack",
  "id": 7,
  "stage": "encrypted",
  "cipher": "Xm4.tQ>aL!z..."
}
```

```json
{
  "type": "tx_ack",
  "id": 7,
  "stage": "transmitted"
}
```

```json
{
  "type": "tx_ack",
  "id": 7,
  "stage": "fail"
}
```

| Field | Type | Description |
|---|---|---|
| `id` | uint16 | Echoes the `id` from the originating `tx` command. |
| `stage` | string | `encrypted`, `transmitted`, or `fail`. |
| `cipher` | string | Present only on `stage: encrypted`. The ciphertext that will be transmitted, for inline display. |

#### `status` — Status report

Sent in response to a `status` request, on startup, and after UART reinitialization.

```json
{
  "type": "status",
  "radio": "ok",
  "key": "nvs",
  "node_id": "C0DC"
}
```

| Field | Type | Description |
|---|---|---|
| `radio` | string | `"ok"` if SX1262 initialized successfully, `"err"` otherwise. |
| `key` | string | `"nvs"` if rotor start and plugboard were loaded from NVS; `"def"` if using compiled-in defaults. |
| `node_id` | string | The Heltec's current node ID. |

#### `node_id_ack` — Node ID acknowledgment

Sent after a successful `set_node_id` command.

```json
{
  "type": "node_id_ack",
  "node_id": "C0DC"
}
```

---

## Message Pipeline

Each sent message moves through the following states, tracked by the CrowPanel:

| Stage | Display indicator | Trigger |
|---|---|---|
| `PENDING` | `----` | `tx` command sent to Heltec, no `tx_ack` yet |
| `SENT` | `>>>>` | `tx_ack` received with `stage: transmitted` |
| `FAILED` | `FAIL` | `tx_ack` received with `stage: fail`, or any error condition |

The `encrypted` stage is internal — the CrowPanel uses it to capture the ciphertext for the ENC toggle display but does not change the visible indicator. The message stays at `----` (PENDING) until `transmitted` or `fail`.

---

## LoRa RF Layer

| Parameter | Value |
|---|---|
| Frequency | 915.0 MHz (US ISM band) |
| Bandwidth | 125 kHz |
| Spreading factor | 7 |
| Coding rate | 4/5 |
| Max payload | 250 bytes |
| Mode | Broadcast, no addressing, no ACK |

All stations receive all traffic. There is no destination addressing in the LoRa packet — any station with a matching cipher configuration will decrypt any packet. Stations without the matching configuration see noise.

The Heltec uses interrupt-driven receive (DIO1 ISR sets a flag; main loop reads the packet). TX is blocking. After TX completes, the ISR flag is cleared before re-entering RX mode to prevent self-reception.

---

## Cipher Specification

### Alphabet

The cipher operates on 94 printable ASCII characters: codepoints 32 through 125 inclusive. Tilde `~` (126) is excluded to produce an even-length alphabet, which guarantees zero fixed points in the reflector.

Characters outside this range are stripped from plaintext before encryption. The `|` character (ASCII 124) is within the cipher range and encrypts along with all other characters.

Mapping: `index = (ASCII - 32) % 94`. Output maps back: `ASCII = index + 32`.

### Packet block format

The complete plaintext block encrypted per packet is:

```
CALLSIGN|MESSAGE
```

The entire block — callsign, pipe, and message — is passed to the cipher as a single string. The `|` separator is not added after encryption; it is part of the encrypted material. The receiver decrypts the full block and splits on the first `|` to recover callsign and message. A packet that decrypts to a string with no `|` is discarded.

### Rotor advance schedule

```
Right rotor   — advances every character
Middle rotor  — advances every 94 characters
Left rotor    — advances every 8836 characters (94²)
```

Character count advances *after* each character is processed. Rotors are reset to their starting positions at the beginning of each call to `processString()` — each message block is encrypted from a known state.

### Signal path per character

```
index (0–93)
  → plugboard
  → Rotor R (forward)
  → Rotor M (forward)
  → Rotor L (forward)
  → reflector
  → Rotor L (reverse)
  → Rotor M (reverse)
  → Rotor R (reverse)
  → plugboard
  → output index (0–93)
```

The operation is symmetric: `encrypt(encrypt(x)) == x` for any input. The same function encrypts and decrypts. Both units must start from identical rotor positions for each message block.

### Rotor forward pass

For a rotor with wiring table `W`, inverse table `W⁻¹`, and current offset `O`:

```
forward(in):
  shifted = (in + O) mod 94
  subst   = W[shifted]
  out     = (subst - O + 94) mod 94

reverse(in):
  shifted = (in + O) mod 94
  subst   = W⁻¹[shifted]
  out     = (subst - O + 94) mod 94
```

The inverse table is precomputed at init: `W⁻¹[W[i]] = i` for all i.

### Reflector constraints

The reflector must be an involution with zero fixed points: `ref[ref[i]] == i` and `ref[i] != i` for all i in 0–93. The even-length alphabet (94) makes a zero-fixed-point involution possible.

### Plugboard

The plugboard is a symmetric swap table: `plug[plug[i]] == i` for all i. The identity table (no swaps) is the default; custom swap pairs can be configured in `config_user.h`.

### Key material

```
Secret (compile-time):  rotor wiring tables (L, M, R), reflector table
Configurable (NVS):     rotor starting positions [L, M, R], plugboard swap table
Runtime:                node ID (NVS, defaults to WiFi MAC last 4 hex)
```

Both paired units must have identical wiring tables, reflector, starting positions, and plugboard to communicate. The wiring tables are the primary secret and are compiled into the firmware. See `config_user.h.example` for the override mechanism.

---

## NVS Layout

### Heltec

| Namespace | Key | Type | Description |
|---|---|---|---|
| `enigma` | `rotor_start` | bytes (3) | Rotor starting positions [L, M, R] |
| `enigma` | `plugboard` | bytes (94) | Plugboard swap table |
| `enigma_id` | `node_id` | string | Node identifier (up to 8 chars) |

### CrowPanel

| Namespace | Key | Type | Description |
|---|---|---|---|
| `enigma` | `callsign` | string | User callsign displayed on sent messages |
| `enigma` | `node_id` | string | Cached copy of Heltec node ID |
| `enigma` | `sound` | bool | Sound enabled flag |

---

## Compatibility Notes

A compatible third-party implementation must:

1. Use the same 94-character alphabet (ASCII 32–125).
2. Use the same rotor advance schedule (R every char, M every 94, L every 8836).
3. Reset all rotor offsets to starting positions before encrypting each message block.
4. Format the plaintext block as `CALLSIGN|MESSAGE` before encryption.
5. Split the decrypted block on the first `|` to recover callsign and message.
6. Discard received packets that decrypt to a string with no `|`.
7. Use the same wiring tables, reflector, starting positions, and plugboard as the paired unit.
8. Communicate over UART at 115200 baud 8N1 using newline-delimited JSON if integrating with a CrowPanel UI host.
