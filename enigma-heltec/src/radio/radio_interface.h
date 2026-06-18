#pragma once
#include <Arduino.h>

// Abstract radio interface
// Concrete implementations: EspNowRadio, LoRaRadio
// main.cpp only ever calls this interface
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
