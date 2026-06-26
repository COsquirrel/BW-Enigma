// Copyright (C) 2025 Mike Barnett / Badger Works
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <Arduino.h>

/* Abstract radio interface — concrete implementation: LoRaRadio (lora_radio.h) */
class RadioInterface {
public:
    virtual ~RadioInterface() {}
    virtual bool   init() = 0;
    virtual bool   send(const String& payload) = 0;
    virtual bool   available() = 0;
    virtual String receive() = 0;
    virtual int    lastRssi() { return 0; }
    virtual float  lastSnr()  { return 0.0f; }
};
