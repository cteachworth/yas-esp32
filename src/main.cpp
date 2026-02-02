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
#include <BluetoothSerial.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <Preferences.h>

#include "config.h"
#include "debug.h"
#include "state.h"
#include "bluetooth.h"
#include "mqtt_client.h"
#include "http_handlers.h"

// ============================================================================
// Global Objects
// ============================================================================

BluetoothSerial SerialBT;
WebServer server(HTTP_PORT);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences prefs;

// ============================================================================
// Connection State and Statistics
// ============================================================================

BtStats btStats;
bool isPaired = false;
bool btConnected = false;
unsigned long lastBtConnectAttempt = 0;
unsigned long reconnectHoldOffUntil = 0;

// Status tracking
String lastBtStatus = "initializing";
String lastPublishedBtStatus = "";
YasStatus lastSoundbarStatus = {false, "unknown", false, 0, 0, "unknown", false, false, false};

// Internal state
static unsigned long lastWifiCheck = 0;
static unsigned long lastMqttConnectAttempt = 0;
static unsigned long lastStatusPoll = 0;
static float lastTemperature = 0.0;
static volatile bool wifiGotIP = false;

// ============================================================================
// Status Helper
// ============================================================================

void setBtStatus(const String& status, const String& detail) {
    lastBtStatus = status;
    if (detail.length() > 0) {
        btStats.lastError = detail;
        DBG("BT STATUS: %s (%s)", status.c_str(), detail.c_str());
    } else {
        DBG("BT STATUS: %s", status.c_str());
    }
}

// ============================================================================
// WiFi
// ============================================================================

static void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            wifiGotIP = true;
            DBG("WiFi: Got IP %s", WiFi.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            wifiGotIP = false;
            DBG("WiFi: Disconnected");
            break;
        default:
            break;
    }
}

static void checkWifiConnection() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    if (millis() - lastWifiCheck < WIFI_RECONNECT_DELAY_MS) {
        return;
    }

    lastWifiCheck = millis();
    DBG("WiFi: Disconnected, reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n");
    Serial.println("========================================");
    Serial.println("  YAS Bluetooth Bridge v2.2.0");
    Serial.println("========================================");
    DBG("ESP32 MAC: %s", WiFi.macAddress().c_str());
    DBG("Free heap: %d bytes", ESP.getFreeHeap());

    // Connect to WiFi
    WiFi.onEvent(WiFiEvent);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    DBG("WiFi: Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (!wifiGotIP && attempts < 60) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (wifiGotIP) {
        DBG("WiFi: Connected! IP: %s, RSSI: %d dBm",
            WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        DBG("WiFi: Connection failed after %d attempts, restarting...", attempts);
        delay(1000);
        ESP.restart();
    }

    // Load pairing state from NVS
    prefs.begin("yas-bridge", false);
    isPaired = prefs.getBool("paired", false);
    DBG("BT: Paired state from NVS: %s", isPaired ? "YES" : "NO");

    // Initialize modules
    initBluetooth();
    initMqtt();
    initHttpServer();

    // Initial connections
    connectBluetooth();
    connectMqtt();

    DBG("Setup complete, entering main loop");
    Serial.println("----------------------------------------");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
    server.handleClient();
    mqtt.loop();
    checkWifiConnection();

    // Publish any pending BT status changes
    publishBtStatus();

    // Check Bluetooth connection state
    bool isConnected = SerialBT.connected();

    // Detect disconnection
    if (!isConnected && btConnected) {
        unsigned long connectedDuration = millis() - btStats.connectedSince;
        btStats.totalConnectedTime += connectedDuration;
        btStats.disconnects++;

        DBG("BT: Connection LOST after %lu ms (total disconnects: %lu)",
            connectedDuration, btStats.disconnects);

        setBtStatus("disconnected");
        btConnected = false;

        if (mqtt.connected()) {
            mqtt.publish(MQTT_AVAILABLE_TOPIC, "offline", true);
            publishBtStatus();
        }
    }

    // Detect new connection
    if (isConnected && !btConnected) {
        btConnected = true;
        btStats.connectedSince = millis();

        DBG("BT: Connection ESTABLISHED");
        setBtStatus("connected");

        if (mqtt.connected()) {
            mqtt.publish(MQTT_AVAILABLE_TOPIC, "online", true);
            publishBtStatus();
        }
    }

    // Reconnect if not connected (but respect hold-off period)
    if (!btConnected) {
        if (millis() >= reconnectHoldOffUntil &&
            millis() - lastBtConnectAttempt > BT_RECONNECT_DELAY_MS) {
            connectBluetooth();
        }
    }

    // Reconnect MQTT if needed
    if (!mqtt.connected() && millis() - lastMqttConnectAttempt > MQTT_RECONNECT_DELAY_MS) {
        lastMqttConnectAttempt = millis();
        connectMqtt();
    }

    // Poll soundbar status periodically
    if (btConnected && millis() - lastStatusPoll > STATUS_POLL_INTERVAL_MS) {
        lastStatusPoll = millis();
        YasStatus status = requestStatus();
        if (status.valid) {
            if (status.power != lastSoundbarStatus.power ||
                status.input != lastSoundbarStatus.input ||
                status.muted != lastSoundbarStatus.muted ||
                status.volume != lastSoundbarStatus.volume ||
                status.subwoofer != lastSoundbarStatus.subwoofer ||
                status.surround != lastSoundbarStatus.surround ||
                status.bass_ext != lastSoundbarStatus.bass_ext ||
                status.clear_voice != lastSoundbarStatus.clear_voice) {
                lastSoundbarStatus = status;
                publishStatus(status);
            }
        }

        // Read ESP32 internal temperature
        float currentTemp = temperatureRead();
        if (abs(currentTemp - lastTemperature) > 0.5) {
            lastTemperature = currentTemp;
            if (mqtt.connected()) {
                String tempTopic = String(MQTT_BASE_TOPIC) + "/temperature";
                mqtt.publish(tempTopic.c_str(), String(currentTemp, 1).c_str(), true);
            }
        }
    }

    yield();
}
