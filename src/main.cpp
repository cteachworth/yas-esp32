/**
 * YAS Bluetooth Bridge for ESP32
 *
 * Connects to a Yamaha YAS soundbar via Bluetooth Classic SPP
 * and exposes both HTTP and MQTT APIs for control from Home Assistant.
 *
 * Hardware: ESP32-WROOM (not ESP32-S2/S3/C3 - needs Classic Bluetooth)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <BluetoothSerial.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "config.h"
#include "yas_commands.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error "Bluetooth Classic is not enabled! Please use an ESP32 board (not S2/S3/C3)"
#endif

#if !defined(CONFIG_BT_SPP_ENABLED)
#error "Serial Bluetooth is not available or not enabled"
#endif

BluetoothSerial SerialBT;
WebServer server(HTTP_PORT);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// State
bool btConnected = false;
unsigned long lastBtConnectAttempt = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastMqttConnectAttempt = 0;
unsigned long lastStatusPoll = 0;
volatile bool wifiGotIP = false;
YasStatus lastStatus = {false, "unknown", false, 0, 0, "unknown", false, false, false};

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            wifiGotIP = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            wifiGotIP = false;
            break;
        default:
            break;
    }
}

// Forward declarations
void handleRoot();
void handleStatus();
void handleSendCommand();
void handleNotFound();
bool checkAuth();
void connectBluetooth();
void checkWifiConnection();
void connectMqtt();
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool sendCommand(const String& cmd);
YasStatus requestStatus();
void publishStatus(const YasStatus& status);
void publishDiscovery();
void setVolume(int targetVolume);
void setSubwoofer(int targetSubwoofer);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\nYAS Bluetooth Bridge starting...");
    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());

    // Connect to WiFi
    WiFi.onEvent(WiFiEvent);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (!wifiGotIP && attempts < 60) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (wifiGotIP) {
        Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection failed! Restarting...");
        delay(1000);
        ESP.restart();
    }

    // Initialize Bluetooth
    Serial.println("Initializing Bluetooth...");
    if (!SerialBT.begin(BT_DEVICE_NAME, true)) {
        Serial.println("Bluetooth initialization failed!");
        ESP.restart();
    }
    Serial.println("Bluetooth initialized");

    // Set up MQTT
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(1024);

    // Set up HTTP routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/send_command", HTTP_GET, handleSendCommand);
    server.onNotFound(handleNotFound);

    const char* headerKeys[] = {"Authorization"};
    server.collectHeaders(headerKeys, 1);

    server.begin();
    Serial.printf("HTTP server started on port %d\n", HTTP_PORT);

    connectBluetooth();
    connectMqtt();
}

void loop() {
    server.handleClient();
    mqtt.loop();
    checkWifiConnection();

    if (!btConnected || !SerialBT.connected()) {
        btConnected = false;
        if (millis() - lastBtConnectAttempt > BT_RECONNECT_DELAY_MS) {
            connectBluetooth();
        }
    }

    if (!mqtt.connected() && millis() - lastMqttConnectAttempt > MQTT_RECONNECT_DELAY_MS) {
        connectMqtt();
    }

    // Poll status periodically to catch remote control changes
    if (btConnected && millis() - lastStatusPoll > STATUS_POLL_INTERVAL_MS) {
        lastStatusPoll = millis();
        YasStatus status = requestStatus();
        if (status.valid) {
            // Check if status changed
            if (status.power != lastStatus.power ||
                status.input != lastStatus.input ||
                status.muted != lastStatus.muted ||
                status.volume != lastStatus.volume ||
                status.subwoofer != lastStatus.subwoofer ||
                status.surround != lastStatus.surround ||
                status.bass_ext != lastStatus.bass_ext ||
                status.clear_voice != lastStatus.clear_voice) {
                lastStatus = status;
                publishStatus(status);
            }
        }
    }

    yield();
}

void checkWifiConnection() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    if (millis() - lastWifiCheck < WIFI_RECONNECT_DELAY_MS) {
        return;
    }

    lastWifiCheck = millis();
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectBluetooth() {
    lastBtConnectAttempt = millis();

    // Clean up any existing connection first
    if (SerialBT.connected()) {
        Serial.println("Disconnecting existing Bluetooth connection...");
        SerialBT.disconnect();
        delay(500);  // Give time for cleanup
    }

    Serial.println("Connecting to soundbar...");

    // Try MAC address first (more reliable, skips discovery)
    uint8_t addr[6];
    if (sscanf(SOUNDBAR_ADDRESS, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) == 6) {
        // Check it's not all zeros
        bool validAddr = false;
        for (int i = 0; i < 6; i++) {
            if (addr[i] != 0) { validAddr = true; break; }
        }
        if (validAddr && SerialBT.connect(addr)) {
            Serial.println("Bluetooth connected (MAC)!");

            // CRITICAL: Wait for SPP link to fully establish
            delay(1000);

            // Verify connection is still valid
            if (SerialBT.connected()) {
                btConnected = true;
                lastStatusPoll = millis();  // Reset to prevent immediate polling

                if (mqtt.connected()) {
                    mqtt.publish(MQTT_AVAILABLE_TOPIC, "online", true);
                }
                Serial.println("Bluetooth connection stabilized");
                return;
            } else {
                Serial.println("Bluetooth connection lost during stabilization");
            }
        }
    }

    // Fallback to name-based discovery
    if (SerialBT.connect(SOUNDBAR_NAME)) {
        Serial.println("Bluetooth connected (name)!");

        // CRITICAL: Wait for SPP link to fully establish
        delay(1000);

        // Verify connection is still valid
        if (SerialBT.connected()) {
            btConnected = true;
            lastStatusPoll = millis();  // Reset to prevent immediate polling

            if (mqtt.connected()) {
                mqtt.publish(MQTT_AVAILABLE_TOPIC, "online", true);
            }
            Serial.println("Bluetooth connection stabilized");
            return;
        } else {
            Serial.println("Bluetooth connection lost during stabilization");
        }
    }

    Serial.println("Bluetooth connection failed");
    btConnected = false;
    if (mqtt.connected()) {
        mqtt.publish(MQTT_AVAILABLE_TOPIC, "offline", true);
    }
}

void connectMqtt() {
    lastMqttConnectAttempt = millis();
    Serial.println("Connecting to MQTT...");

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
        Serial.println("MQTT connected!");
        mqtt.publish(MQTT_AVAILABLE_TOPIC, btConnected ? "online" : "offline", true);
        mqtt.subscribe(MQTT_COMMAND_TOPIC);
        mqtt.subscribe(MQTT_VOLUME_TOPIC);
        mqtt.subscribe(MQTT_SUBWOOFER_TOPIC);
        publishDiscovery();

        // Publish current status
        if (btConnected) {
            YasStatus status = requestStatus();
            if (status.valid) {
                lastStatus = status;
                publishStatus(status);
            }
        }
    } else {
        Serial.printf("MQTT connection failed, rc=%d\n", mqtt.state());
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.printf("[MQTT] %s: %s\n", topic, message.c_str());

    if (String(topic) == MQTT_COMMAND_TOPIC) {
        if (isValidCommand(message)) {
            if (sendCommand(message)) {
                delay(100);
                YasStatus status = requestStatus();
                if (status.valid) {
                    lastStatus = status;
                    publishStatus(status);
                }
            }
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
    }
}

void setVolume(int targetVolume) {
    if (!btConnected) return;

    YasStatus status = requestStatus();
    if (!status.valid) return;

    int diff = targetVolume - status.volume;
    String cmd = (diff > 0) ? "volume_up" : "volume_down";
    int steps = abs(diff);

    for (int i = 0; i < steps && i < 50; i++) {
        sendCommand(cmd);
        delay(50);
    }

    delay(100);
    status = requestStatus();
    if (status.valid) {
        lastStatus = status;
        publishStatus(status);
    }
}

void setSubwoofer(int targetSubwoofer) {
    if (!btConnected) return;

    YasStatus status = requestStatus();
    if (!status.valid) return;

    int diff = targetSubwoofer - status.subwoofer;
    String cmd = (diff > 0) ? "subwoofer_up" : "subwoofer_down";
    // Each command changes value by 4, so divide by 4
    int steps = abs(diff) / 4;

    for (int i = 0; i < steps && i < 8; i++) {
        sendCommand(cmd);
        delay(50);
    }

    delay(100);
    status = requestStatus();
    if (status.valid) {
        lastStatus = status;
        publishStatus(status);
    }
}

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
}

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

    Serial.println("MQTT discovery published");
}

bool checkAuth() {
    String apiKey = String(API_KEY);
    if (apiKey.length() == 0) {
        return true;
    }

    if (server.hasHeader("Authorization")) {
        String auth = server.header("Authorization");
        if (auth.startsWith("Bearer ")) {
            auth = auth.substring(7);
        }
        if (auth == apiKey) {
            return true;
        }
    }

    if (server.hasArg("api_key") && server.arg("api_key") == apiKey) {
        return true;
    }

    server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return false;
}

void handleRoot() {
    if (!checkAuth()) return;

    JsonDocument doc;
    doc["name"] = "YAS Bluetooth Bridge";
    doc["version"] = "2.0.0";
    doc["bluetooth_connected"] = btConnected;
    doc["mqtt_connected"] = mqtt.connected();
    doc["ip"] = WiFi.localIP().toString();

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleStatus() {
    if (!checkAuth()) return;

    if (!btConnected) {
        server.send(503, "application/json", "{\"error\":\"Bluetooth not connected\"}");
        return;
    }

    YasStatus status = requestStatus();

    if (!status.valid) {
        server.send(500, "application/json", "{\"error\":\"Failed to get status\"}");
        return;
    }

    JsonDocument doc;
    doc["power"] = status.power;
    doc["input"] = status.input;
    doc["muted"] = status.muted;
    doc["volume"] = status.volume;
    doc["subwoofer"] = status.subwoofer;
    doc["surround"] = status.surround;
    doc["bass_ext"] = status.bass_ext;
    doc["clear_voice"] = status.clear_voice;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleSendCommand() {
    if (!checkAuth()) return;

    if (!server.hasArg("command")) {
        server.send(400, "application/json", "{\"error\":\"Missing required parameter: command\"}");
        return;
    }

    String command = server.arg("command");

    if (!isValidCommand(command)) {
        server.send(400, "application/json", "{\"error\":\"Invalid command\"}");
        return;
    }

    if (!btConnected) {
        server.send(503, "application/json", "{\"error\":\"Bluetooth not connected\"}");
        return;
    }

    if (sendCommand(command)) {
        server.send(200, "application/json", "{\"message\":\"Command sent\"}");
    } else {
        server.send(500, "application/json", "{\"error\":\"Failed to send command\"}");
    }
}

void handleNotFound() {
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
}

bool sendCommand(const String& cmd) {
    String encoded = encodeCommand(cmd);
    if (encoded.length() == 0) {
        return false;
    }

    uint8_t buffer[32];
    int len = hexStringToBytes(encoded, buffer, sizeof(buffer));
    size_t written = SerialBT.write(buffer, len);
    return written == (size_t)len;
}

YasStatus requestStatus() {
    YasStatus status = {false, "unknown", false, 0, 0, "unknown", false, false, false};

    while (SerialBT.available()) {
        SerialBT.read();
    }

    if (!sendCommand("report_status")) {
        return status;
    }

    unsigned long startTime = millis();

    while (millis() - startTime < STATUS_REQUEST_TIMEOUT_MS) {
        if (SerialBT.available()) {
            uint8_t buffer[64];
            int len = 0;

            while (SerialBT.available() && len < (int)sizeof(buffer)) {
                buffer[len++] = SerialBT.read();
                delay(5);
            }

            String response = bytesToHexString(buffer, len);
            status = decodeStatus(response);
            if (status.valid) {
                return status;
            }
        }
        delay(10);
    }

    return status;
}
