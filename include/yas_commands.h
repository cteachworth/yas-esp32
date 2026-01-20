#ifndef YAS_COMMANDS_H
#define YAS_COMMANDS_H

#include <Arduino.h>
#include <map>

// Command payloads (without framing)
const std::map<String, String> COMMANDS = {
    // Power management
    {"power_toggle", "4078cc"},
    {"power_on", "40787e"},
    {"power_off", "40787f"},

    // Input management
    {"set_input_hdmi", "40784a"},
    {"set_input_analog", "4078d1"},
    {"set_input_bluetooth", "407829"},
    {"set_input_tv", "4078df"},

    // Surround management
    {"set_surround_3d", "4078c9"},
    {"set_surround_tv", "407ef1"},
    {"set_surround_stereo", "407850"},
    {"set_surround_movie", "4078d9"},
    {"set_surround_music", "4078da"},
    {"set_surround_sports", "4078db"},
    {"set_surround_game", "4078dc"},
    {"surround_toggle", "4078b4"},
    {"clearvoice_toggle", "40785c"},
    {"clearvoice_on", "407e80"},
    {"clearvoice_off", "407e82"},
    {"bass_ext_toggle", "40788b"},
    {"bass_ext_on", "40786e"},
    {"bass_ext_off", "40786f"},

    // Volume management
    {"subwoofer_up", "40784c"},
    {"subwoofer_down", "40784d"},
    {"mute_toggle", "40789c"},
    {"mute_on", "407ea2"},
    {"mute_off", "407ea3"},
    {"volume_up", "40781e"},
    {"volume_down", "40781f"},

    // Extra
    {"bluetooth_standby_toggle", "407834"},
    {"dimmer", "4078ba"},

    // Status report
    {"report_status", "0305"}
};

// Input name mapping
const std::map<String, String> INPUT_NAMES = {
    {"00", "hdmi"},
    {"0c", "analog"},
    {"05", "bluetooth"},
    {"07", "tv"}
};

// Surround name mapping
const std::map<String, String> SURROUND_NAMES = {
    {"000d", "3d"},
    {"000a", "tv"},
    {"0100", "stereo"},
    {"0003", "movie"},
    {"0008", "music"},
    {"0009", "sports"},
    {"000c", "game"}
};

// Check if command is valid
inline bool isValidCommand(const String& cmd) {
    return COMMANDS.find(cmd) != COMMANDS.end();
}

// Encode a command with framing
// Format: ccaa <length> <payload> <checksum>
inline String encodeCommand(const String& cmd) {
    auto it = COMMANDS.find(cmd);
    if (it == COMMANDS.end()) {
        return "";
    }

    String payload = it->second;
    int payloadLen = payload.length() / 2;

    // Calculate checksum
    int sum = payloadLen;
    for (size_t i = 0; i < payload.length(); i += 2) {
        sum += strtol(payload.substring(i, i + 2).c_str(), NULL, 16);
    }
    int checksum = (-sum) & 0xFF;

    // Build framed command
    char result[32];
    snprintf(result, sizeof(result), "ccaa%02x%s%02x", payloadLen, payload.c_str(), checksum);
    return String(result);
}

// Convert hex string to byte array
inline int hexStringToBytes(const String& hex, uint8_t* buffer, int maxLen) {
    int len = hex.length() / 2;
    if (len > maxLen) len = maxLen;

    for (int i = 0; i < len; i++) {
        buffer[i] = strtol(hex.substring(i * 2, i * 2 + 2).c_str(), NULL, 16);
    }
    return len;
}

// Convert byte array to hex string
inline String bytesToHexString(const uint8_t* buffer, int len) {
    String result = "";
    for (int i = 0; i < len; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", buffer[i]);
        result += hex;
    }
    return result;
}

// Status structure
struct YasStatus {
    bool power;
    String input;
    bool muted;
    int volume;
    int subwoofer;
    String surround;
    bool bass_ext;
    bool clear_voice;
    bool valid;
};

// Decode status response
// Format: ccaa 0d 05 00 <power> <input> <muted> <volume> <subwoofer> 20 20 00 <surround 2B> <be+cv>
inline YasStatus decodeStatus(const String& hex) {
    YasStatus status = {false, "unknown", false, 0, 0, "unknown", false, false, false};

    if (hex.length() < 32) {
        return status;
    }

    // Check if this is a status message (type = 05)
    if (hex.substring(6, 8) != "05") {
        return status;
    }

    status.valid = true;
    status.power = hex.substring(10, 12) == "01";

    String inputBit = hex.substring(12, 14);
    auto inputIt = INPUT_NAMES.find(inputBit);
    status.input = (inputIt != INPUT_NAMES.end()) ? inputIt->second : "unknown";

    status.muted = hex.substring(14, 16) == "01";
    status.volume = strtol(hex.substring(16, 18).c_str(), NULL, 16);
    status.subwoofer = strtol(hex.substring(18, 20).c_str(), NULL, 16);

    String surroundBit = hex.substring(26, 30);
    auto surroundIt = SURROUND_NAMES.find(surroundBit);
    status.surround = (surroundIt != SURROUND_NAMES.end()) ? surroundIt->second : "unknown";

    // Bass extension: check if high nibble of byte at position 30 is 0x2X
    // Clear voice: check if low nibble of byte at position 30 is 0xX4
    status.bass_ext = hex.substring(30, 31) == "2";
    status.clear_voice = hex.substring(31, 32) == "4";

    return status;
}

#endif
