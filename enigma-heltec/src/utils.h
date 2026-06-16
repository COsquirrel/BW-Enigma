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

// -----------------------------------------------------------------------
// Quick test - call from setup() to verify both functions
// -----------------------------------------------------------------------
void testUtils(Stream& ser) {
    ser.println("--- Utils self-test ---");

    // Sanitizer tests
    struct { const char* input; const char* expected; } sanTests[] = {
        {"Hello World",          "Hello World"},
        {"Hello\nWorld",         "Hello World"},    // newline stripped
        {"Hello\tWorld",         "Hello World"},    // tab stripped
        {"  Hello  World  ",     "Hello World"},    // spaces trimmed/collapsed
        {"Hello\x01World",       "HelloWorld"},     // control char dropped
        {"",                     ""},
        {"   ",                  ""},
        {"Hello World 123!",     "Hello World 123!"},
    };

    bool allOk = true;
    for (auto& t : sanTests) {
        String got = sanitizeInput(String(t.input));
        bool ok = (got == String(t.expected));
        if (!ok) {
            ser.print("  SANITIZE FAIL: input=");
            ser.print(t.input);
            ser.print(" expected=");
            ser.print(t.expected);
            ser.print(" got=");
            ser.println(got);
            allOk = false;
        }
    }
    if (allOk) ser.println("  Sanitizer: PASS");

    // Word wrap tests
    ser.println("  Word wrap (21 chars/line):");
    String longMsg = "The quick brown fox jumps over the lazy dog";
    WrappedText wt = wordWrap(longMsg);
    for (int i = 0; i < wt.count; i++) {
        ser.print("    [");
        ser.print(i);
        ser.print("] \"");
        ser.print(wt.lines[i]);
        ser.println("\"");
    }

    String shortMsg = "Hi Mike";
    WrappedText wt2 = wordWrap(shortMsg);
    ser.print("  Short msg lines: ");
    ser.println(wt2.count);

    ser.println("--- Utils test done ---");
}
