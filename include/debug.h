#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

// Timestamp helper
inline String timestamp() {
    unsigned long ms = millis();
    unsigned long sec = ms / 1000;
    unsigned long min = sec / 60;
    char buf[16];
    sprintf(buf, "[%02lu:%02lu.%03lu] ", min % 60, sec % 60, ms % 1000);
    return String(buf);
}

// Debug print with timestamp
#define DBG(fmt, ...) Serial.printf("%s" fmt "\n", timestamp().c_str(), ##__VA_ARGS__)

// Convert bytes to hex string for logging
inline String bytesToHex(const uint8_t* data, int len) {
    String result;
    result.reserve(len * 3);
    for (int i = 0; i < len; i++) {
        if (i > 0) result += ' ';
        char buf[3];
        sprintf(buf, "%02X", data[i]);
        result += buf;
    }
    return result;
}

#endif
