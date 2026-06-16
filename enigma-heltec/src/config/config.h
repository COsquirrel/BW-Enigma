#pragma once
#include <stdint.h>

// Operate over printable ASCII 32-125 (94 characters, even number)
// Tilde ~ (126) excluded to achieve even range - zero reflector fixed points
#define PRINTABLE_START 32
#define PRINTABLE_END   125
#define CIPHER_RANGE    94

#define NUM_ROTORS      3
#define ROTOR_PERIOD_R  1
#define ROTOR_PERIOD_M  94
#define ROTOR_PERIOD_L  8836

#define RADIO_ESPNOW    0
#define RADIO_LORA      1
#define RADIO_MODE      RADIO_LORA

// Hardcoded MAC addresses for both units
#define UNIT1_MAC       {0xAC, 0xA7, 0x04, 0x3C, 0xC0, 0xDC}  // TODO: fill in Unit 1 MAC AC:A7:04:3C:C0:DC
#define UNIT2_MAC       {0xAC, 0xA7, 0x04, 0x3C, 0xC0, 0xE4}  // Unit 2 MAC AC:A7:04:3C:C0:E4


#define OLED_VEXT       36   // Heltec LoRa V3: GPIO36 powers OLED/peripherals
#define OLED_SDA        17
#define OLED_SCL        18
#define OLED_RST        21
#define OLED_ADDR       0x3C
#define OLED_CONTRAST   0xCF  // 0x00-0xFF; default SWITCHCAPVCC is 0xCF
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64

#define ROLE_UNIT1      0
#define ROLE_UNIT2      1
#define UNIT_ROLE       ROLE_UNIT1  // Change to ROLE_UNIT2 when flashing the other device

struct EnigmaConfig {
    uint8_t rotor_wirings[NUM_ROTORS][CIPHER_RANGE];
    uint8_t reflector[CIPHER_RANGE];
    uint8_t rotor_start[NUM_ROTORS];
    uint8_t plugboard[CIPHER_RANGE];
};

// Valid permutation tables - verified bijective, zero fixed points
static const EnigmaConfig DEFAULT_CONFIG = {
    {
        {
        // Rotor L
         70,  59,  61,  62,  33,  26,   1,  67,  15,  60,  19,  10,  52,  73,  51,  87,
         49,  41,  76,  30,  55,  74,  45,  32,  79,  66,  89,   8,  37,  83,  78,  47,
         44,  42,  92,  50,  93,  12,  36,  23,  39,  40,  18,  63,  72,  56,   7,  34,
         77,  46,   2,  16,  38,  65,  22,  58,  24,   5,   6,  21,  48,  86,   9,  68,
         43,  82,  20,   0,  90,  57,  88,  53,  85,  25,  71,  80,  64,  29,  27,  84,
         91,   4,  54,  75,  11,  69,  13,  17,  28,  31,  35,   3,  14,  81,
        },
        {
        // Rotor M
         41,  14,  13,  63,  23,  10,  29,  52,   2,  56,  77,  35,  30,  47,  38,  16,
         93,   9,  85,  76,  46,  57,  86,  27,  72,  49,  80,   6,  70,  59,   5,  91,
         83,  19,  39,   3,  79,  55,  54,  67,  32,  53,  43,   1,  22,  17,  75,  18,
          7,  25,  20,  48,  65,  64,  58,  51,  24,  28,  50,   0,  78,  84,  69,  34,
         15,  61,   8,  11,  42,  66,  40,  90,  31,  60,  82,  68,  26,  71,  73,  92,
         37,  12,   4,  33,  45,  44,  36,  89,  21,  62,  81,  87,  88,  74,
        },
        {
        // Rotor R
          7,   1,  36,  44,  23,  80,  92,  38,  51,  40,  81,  24,  53,  56,  27,  54,
         58,  82,  35,  71,  91,  45,  87,  43,  73,  52,  72,  31,  30,  29,  19,  61,
         83,  16,  20,  46,  69,  11,  13,   6,   2,  10,  75,  12,  33,  17,  86,  15,
         76,  37,  57,  88,  22,  49,   9,  64,  47,  39,  78,  90,  89,  14,  34,  18,
          8,  66,  74,  60,  59,  50,  70,  85,   4,  28,  93,  55,  41,   5,  48,  77,
         84,  26,  68,  32,  25,   0,  21,  65,  62,  79,  67,  63,   3,  42,
        },
    },
    {
        // Reflector
         69,  70,  92,  25,  30,  56,  90,  15,  65,  34,  86,  57,  24,  55,  74,   7,
         61,  89,  45,  33,  71,  87,  75,  42,  12,   3,  64,  36,  66,  51,   4,  58,
         50,  19,   9,  88,  27,  49,  77,  91,  82,  67,  23,  46,  47,  18,  43,  44,
         76,  37,  32,  29,  78,  60,  93,  13,   5,  11,  31,  81,  53,  16,  68,  79,
         26,   8,  28,  41,  62,   0,   1,  20,  73,  72,  14,  22,  48,  38,  52,  63,
         84,  59,  40,  85,  80,  83,  10,  21,  35,  17,   6,  39,   2,  54,
    },
    {42, 17, 93},
    {
        // Plugboard identity
          0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,
         16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,
         32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
         48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
         64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
         80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,
    }
};

#if __has_include("config_user.h")
#include "config_user.h"
#define ACTIVE_CONFIG USER_CONFIG
#else
#define ACTIVE_CONFIG DEFAULT_CONFIG
#endif
