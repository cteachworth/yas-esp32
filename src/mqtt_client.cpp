#include "mqtt_client.h"
#include "state.h"
#include "config.h"
#include "debug.h"
#include "bluetooth.h"
#include "yas_commands.h"

#include <WiFi.h>
#include <ArduinoJson.h>

// Initialize MQTT
void initMqtt() {
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(1024);
    DBG("MQTT: Configured for %s:%d", MQTT_HOST, MQTT_PORT);
}

// Connect to MQTT broker
void connectMqtt() {
    DBG("MQTT: Connecting to %s:%d...", MQTT_HOST, MQTT_PORT);

    String clientId = "yas-bridge-" + String(WiFi.macAddress());
    clientId.replace(":", "");

    bool connected;
    if (strlen(MQTT_USER) > 0) {
        connected = mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD,
                                 MQTT_AVAILABLE_TOPIC, 0, true, "offline");
    } else {
        connected = mqtt.connect(clientId.c_str(), MQTT_AVAILABLE_TOPIC, 0, true, "offline");
    }

    if (connected) {
        DBG("MQTT: Connected!");
        mqtt.publish(MQTT_AVAILABLE_TOPIC, btConnected ? "online" : "offline", true);
        mqtt.subscribe(MQTT_COMMAND_TOPIC);
        mqtt.subscribe(MQTT_VOLUME_TOPIC);
        mqtt.subscribe(MQTT_SUBWOOFER_TOPIC);
        mqtt.subscribe(MQTT_RESTART_TOPIC);
        mqtt.subscribe(MQTT_RESET_PAIRING_TOPIC);
        publishDiscovery();
        lastPublishedBtStatus = "";
        publishBtStatus();

        if (btConnected) {
            YasStatus status = requestStatus();
            if (status.valid) {
                lastSoundbarStatus = status;
                publishStatus(status);
            }
        }
    } else {
        DBG("MQTT: Connection failed, rc=%d", mqtt.state());
    }
}

// MQTT message callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    DBG("MQTT RX: %s = %s", topic, message.c_str());

    if (String(topic) == MQTT_COMMAND_TOPIC) {
        if (isValidCommand(message)) {
            if (sendCommand(message)) {
                delay(100);
                YasStatus status = requestStatus();
                if (status.valid) {
                    lastSoundbarStatus = status;
                    publishStatus(status);
                }
            }
        } else {
            DBG("MQTT: Invalid command: %s", message.c_str());
        }
    } else if (String(topic) == MQTT_VOLUME_TOPIC) {
        int targetVolume = message.toInt();
        if (targetVolume >= 0 && targetVolume <= 50) {
            setVolume(targetVolume);
        }
    } else if (String(topic) == MQTT_SUBWOOFER_TOPIC) {
        int targetSubwoofer = message.toInt();
        if (targetSubwoofer >= 0 && targetSubwoofer <= 32) {
            setSubwoofer(targetSubwoofer);
        }
    } else if (String(topic) == MQTT_RESTART_TOPIC) {
        DBG("MQTT: Restart requested");
        delay(100);
        ESP.restart();
    } else if (String(topic) == MQTT_RESET_PAIRING_TOPIC) {
        DBG("MQTT: Reset pairing requested");
        resetPairing();
    }
}

// Publish BT status changes
void publishBtStatus() {
    if (mqtt.connected() && lastBtStatus != lastPublishedBtStatus) {
        String topic = String(MQTT_BASE_TOPIC) + "/bt_status";
        mqtt.publish(topic.c_str(), lastBtStatus.c_str(), true);
        lastPublishedBtStatus = lastBtStatus;
        DBG("MQTT: Published BT status: %s", lastBtStatus.c_str());
    }
}

// Publish soundbar status
void publishStatus(const YasStatus& status) {
    if (!mqtt.connected()) return;

    JsonDocument doc;
    doc["power"] = status.power ? "ON" : "OFF";
    doc["input"] = status.input;
    doc["muted"] = status.muted ? "ON" : "OFF";
    doc["volume"] = status.volume;
    doc["subwoofer"] = status.subwoofer;
    doc["surround"] = status.surround;
    doc["bass_ext"] = status.bass_ext ? "ON" : "OFF";
    doc["clear_voice"] = status.clear_voice ? "ON" : "OFF";

    String payload;
    serializeJson(doc, payload);
    mqtt.publish(MQTT_STATE_TOPIC, payload.c_str(), true);

    DBG("MQTT TX: State published");
}

// Set volume (stepped)
void setVolume(int targetVolume) {
    if (!btConnected) {
        DBG("Volume: Not connected");
        return;
    }

    YasStatus status = requestStatus();
    if (!status.valid) {
        DBG("Volume: Failed to get current status");
        return;
    }

    int diff = targetVolume - status.volume;
    if (diff == 0) return;

    String cmd = (diff > 0) ? "volume_up" : "volume_down";
    int steps = abs(diff);

    DBG("Volume: %d -> %d (%d steps)", status.volume, targetVolume, steps);

    for (int i = 0; i < steps && i < 50; i++) {
        sendCommand(cmd);
        delay(50);
    }

    delay(100);
    status = requestStatus();
    if (status.valid) {
        lastSoundbarStatus = status;
        publishStatus(status);
        DBG("Volume: Now at %d", status.volume);
    }
}

// Set subwoofer level (stepped by 4)
void setSubwoofer(int targetSubwoofer) {
    if (!btConnected) {
        DBG("Subwoofer: Not connected");
        return;
    }

    YasStatus status = requestStatus();
    if (!status.valid) {
        DBG("Subwoofer: Failed to get current status");
        return;
    }

    int diff = targetSubwoofer - status.subwoofer;
    if (diff == 0) return;

    String cmd = (diff > 0) ? "subwoofer_up" : "subwoofer_down";
    int steps = abs(diff) / 4;

    DBG("Subwoofer: %d -> %d (%d steps)", status.subwoofer, targetSubwoofer, steps);

    for (int i = 0; i < steps && i < 8; i++) {
        sendCommand(cmd);
        delay(50);
    }

    delay(100);
    status = requestStatus();
    if (status.valid) {
        lastSoundbarStatus = status;
        publishStatus(status);
        DBG("Subwoofer: Now at %d", status.subwoofer);
    }
}

// Publish Home Assistant MQTT discovery
void publishDiscovery() {
    // Power switch
    {
        JsonDocument doc;
        doc["name"] = "Power";
        doc["unique_id"] = "yas_power";
        doc["state_topic"] = MQTT_STATE_TOPIC;
        doc["command_topic"] = MQTT_COMMAND_TOPIC;
        doc["value_template"] = "{{ value_json.power }}";
        doc["payload_on"] = "power_on";
        doc["payload_off"] = "power_off";
        doc["state_on"] = "ON";
        doc["state_off"] = "OFF";
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/switch/yas_soundbar/power/config", payload.c_str(), true);
    }

    // Mute switch
    {
        JsonDocument doc;
        doc["name"] = "Mute";
        doc["unique_id"] = "yas_mute";
        doc["state_topic"] = MQTT_STATE_TOPIC;
        doc["command_topic"] = MQTT_COMMAND_TOPIC;
        doc["value_template"] = "{{ value_json.muted }}";
        doc["payload_on"] = "mute_on";
        doc["payload_off"] = "mute_off";
        doc["state_on"] = "ON";
        doc["state_off"] = "OFF";
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/switch/yas_soundbar/mute/config", payload.c_str(), true);
    }

    // Clear Voice switch
    {
        JsonDocument doc;
        doc["name"] = "Clear Voice";
        doc["unique_id"] = "yas_clear_voice";
        doc["state_topic"] = MQTT_STATE_TOPIC;
        doc["command_topic"] = MQTT_COMMAND_TOPIC;
        doc["value_template"] = "{{ value_json.clear_voice }}";
        doc["payload_on"] = "clearvoice_on";
        doc["payload_off"] = "clearvoice_off";
        doc["state_on"] = "ON";
        doc["state_off"] = "OFF";
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/switch/yas_soundbar/clear_voice/config", payload.c_str(), true);
    }

    // Bass Extension switch
    {
        JsonDocument doc;
        doc["name"] = "Bass Extension";
        doc["unique_id"] = "yas_bass_ext";
        doc["state_topic"] = MQTT_STATE_TOPIC;
        doc["command_topic"] = MQTT_COMMAND_TOPIC;
        doc["value_template"] = "{{ value_json.bass_ext }}";
        doc["payload_on"] = "bass_ext_on";
        doc["payload_off"] = "bass_ext_off";
        doc["state_on"] = "ON";
        doc["state_off"] = "OFF";
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/switch/yas_soundbar/bass_ext/config", payload.c_str(), true);
    }

    // Volume number
    {
        JsonDocument doc;
        doc["name"] = "Volume";
        doc["unique_id"] = "yas_volume";
        doc["state_topic"] = MQTT_STATE_TOPIC;
        doc["command_topic"] = MQTT_VOLUME_TOPIC;
        doc["value_template"] = "{{ value_json.volume }}";
        doc["min"] = 0;
        doc["max"] = 50;
        doc["step"] = 1;
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/number/yas_soundbar/volume/config", payload.c_str(), true);
    }

    // Subwoofer number
    {
        JsonDocument doc;
        doc["name"] = "Subwoofer";
        doc["unique_id"] = "yas_subwoofer";
        doc["state_topic"] = MQTT_STATE_TOPIC;
        doc["command_topic"] = MQTT_SUBWOOFER_TOPIC;
        doc["value_template"] = "{{ value_json.subwoofer }}";
        doc["min"] = 0;
        doc["max"] = 32;
        doc["step"] = 4;
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/number/yas_soundbar/subwoofer/config", payload.c_str(), true);
    }

    // Input select
    {
        JsonDocument doc;
        doc["name"] = "Input";
        doc["unique_id"] = "yas_input";
        doc["state_topic"] = MQTT_STATE_TOPIC;
        doc["command_topic"] = MQTT_COMMAND_TOPIC;
        doc["value_template"] = "{{ value_json.input }}";
        doc["command_template"] = "set_input_{{ value }}";
        doc["options"][0] = "hdmi";
        doc["options"][1] = "analog";
        doc["options"][2] = "bluetooth";
        doc["options"][3] = "tv";
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/select/yas_soundbar/input/config", payload.c_str(), true);
    }

    // Surround select
    {
        JsonDocument doc;
        doc["name"] = "Surround";
        doc["unique_id"] = "yas_surround";
        doc["state_topic"] = MQTT_STATE_TOPIC;
        doc["command_topic"] = MQTT_COMMAND_TOPIC;
        doc["value_template"] = "{{ value_json.surround }}";
        doc["command_template"] = "set_surround_{{ value }}";
        doc["options"][0] = "3d";
        doc["options"][1] = "tv";
        doc["options"][2] = "stereo";
        doc["options"][3] = "movie";
        doc["options"][4] = "music";
        doc["options"][5] = "sports";
        doc["options"][6] = "game";
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/select/yas_soundbar/surround/config", payload.c_str(), true);
    }

    // ESP32 Temperature sensor
    {
        JsonDocument doc;
        doc["name"] = "ESP32 Temperature";
        doc["unique_id"] = "yas_bridge_temperature";
        doc["state_topic"] = String(MQTT_BASE_TOPIC) + "/temperature";
        doc["unit_of_measurement"] = "°C";
        doc["device_class"] = "temperature";
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/sensor/yas_soundbar/temperature/config", payload.c_str(), true);
    }

    // Bluetooth status sensor
    {
        JsonDocument doc;
        doc["name"] = "Bluetooth Status";
        doc["unique_id"] = "yas_bridge_bt_status";
        doc["state_topic"] = String(MQTT_BASE_TOPIC) + "/bt_status";
        doc["icon"] = "mdi:bluetooth";
        doc["availability_topic"] = MQTT_AVAILABLE_TOPIC;
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/sensor/yas_soundbar/bt_status/config", payload.c_str(), true);
    }

    // Restart button
    {
        JsonDocument doc;
        doc["name"] = "Restart Bridge";
        doc["unique_id"] = "yas_bridge_restart";
        doc["command_topic"] = MQTT_RESTART_TOPIC;
        doc["payload_press"] = "restart";
        doc["icon"] = "mdi:restart";
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/button/yas_soundbar/restart/config", payload.c_str(), true);
    }

    // Reset Pairing button
    {
        JsonDocument doc;
        doc["name"] = "Reset Pairing";
        doc["unique_id"] = "yas_soundbar_reset_pairing";
        doc["command_topic"] = MQTT_RESET_PAIRING_TOPIC;
        doc["payload_press"] = "reset";
        doc["icon"] = "mdi:bluetooth-off";
        doc["device"]["identifiers"][0] = "yas_soundbar";
        doc["device"]["name"] = "YAS Soundbar";
        doc["device"]["manufacturer"] = "Yamaha";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish("homeassistant/button/yas_soundbar/reset_pairing/config", payload.c_str(), true);
    }

    DBG("MQTT: Discovery published");
}
