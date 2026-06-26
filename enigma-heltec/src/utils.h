// Copyright (C) 2025 Mike Barnett / Badger Works
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------
// OLED display constants
// Using Adafruit SSD1306 128x64 with default 6x8 font
// -----------------------------------------------------------------------
#define OLED_CHAR_WIDTH   6   // pixels per char at text size 1
#define OLED_SCREEN_W   128   // screen width in pixels
#define OLED_CHARS_PER_LINE  (OLED_SCREEN_W / OLED_CHAR_WIDTH)  // 21 chars

// -----------------------------------------------------------------------
// sanitizeInput()
// Strips any character outside printable ASCII 32-126
// Collapses all whitespace/control chars to a single space
// Trims leading/trailing spaces
// Result is guaranteed safe to pass to enigma.processString()
// -----------------------------------------------------------------------
String sanitizeInput(const String& input) {
    String result = "";
    result.reserve(input.length());
    bool last_was_space = true;  // true to trim leading spaces

    for (int i = 0; i < (int)input.length(); i++) {
        char c = input[i];
        uint8_t val = (uint8_t)c;

        // Treat tab and newline as space
        if (c == '\t' || c == '\n' || c == '\r') { c = ' '; val = 32; }

        if (val >= 32 && val <= 125) {  // 32-125, tilde(126) excluded
            // Printable - collapse multiple spaces
            if (c == ' ') {
                if (!last_was_space) {
                    result += ' ';
                    last_was_space = true;
                }
            } else {
                result += c;
                last_was_space = false;
            }
        }
        // Anything else (tabs, newlines, control chars, extended) - drop it
    }

    // Trim trailing space
    if (result.length() > 0 && result[result.length() - 1] == ' ') {
        result = result.substring(0, result.length() - 1);
    }

    return result;
}

// -----------------------------------------------------------------------
// wordWrap()
// Breaks a string into lines of max `lineWidth` characters
// Breaks at word boundaries (spaces) where possible
// If a single word is longer than lineWidth it gets hard-broken
// Returns a vector-style result via a callback or array
// For Arduino compatibility returns a simple fixed array of Strings
// -----------------------------------------------------------------------
#define MAX_WRAP_LINES 8

struct WrappedText {
    String lines[MAX_WRAP_LINES];
    int    count;
};

WrappedText wordWrap(const String& text, int lineWidth = OLED_CHARS_PER_LINE) {
    WrappedText result;
    result.count = 0;

    if (text.length() == 0) return result;

    int len       = text.length();
    int lineStart = 0;

    while (lineStart < len && result.count < MAX_WRAP_LINES) {
        // How many chars can fit on this line
        int remaining = len - lineStart;

        if (remaining <= lineWidth) {
            // Rest fits on one line
            result.lines[result.count++] = text.substring(lineStart);
            break;
        }

        // Look for last space within lineWidth chars
        int breakAt = -1;
        for (int i = lineStart + lineWidth - 1; i >= lineStart; i--) {
            if (text[i] == ' ') {
                breakAt = i;
                break;
            }
        }

        if (breakAt == -1) {
            // No space found - hard break at lineWidth
            result.lines[result.count++] = text.substring(lineStart, lineStart + lineWidth);
            lineStart += lineWidth;
        } else {
            // Break at space, skip the space itself
            result.lines[result.count++] = text.substring(lineStart, breakAt);
            lineStart = breakAt + 1;
        }
    }

    return result;
}

