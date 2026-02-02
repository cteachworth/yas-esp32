#include "http_handlers.h"
#include "state.h"
#include "config.h"
#include "debug.h"
#include "bluetooth.h"
#include "yas_commands.h"

#include <WiFi.h>
#include <ArduinoJson.h>

// Check API key authentication
static bool checkAuth() {
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

// Initialize HTTP server
void initHttpServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/send_command", HTTP_GET, handleSendCommand);
    server.on("/debug", HTTP_GET, handleDebug);
    server.on("/reset_pairing", HTTP_GET, handleResetPairing);
    server.on("/reconnect", HTTP_GET, handleReconnect);
    server.onNotFound(handleNotFound);

    const char* headerKeys[] = {"Authorization"};
    server.collectHeaders(headerKeys, 1);

    server.begin();
    DBG("HTTP: Server started on port %d", HTTP_PORT);
    DBG("HTTP: Debug endpoint at http://%s/debug", WiFi.localIP().toString().c_str());
}

// GET / - Basic info
void handleRoot() {
    if (!checkAuth()) return;

    JsonDocument doc;
    doc["name"] = "YAS Bluetooth Bridge";
    doc["version"] = "2.2.0";
    doc["bluetooth_connected"] = btConnected;
    doc["mqtt_connected"] = mqtt.connected();
    doc["ip"] = WiFi.localIP().toString();

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// GET /status - Soundbar status
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

// GET /debug - Debug info
void handleDebug() {
    if (!checkAuth()) return;

    JsonDocument doc;

    // System info
    doc["uptime_ms"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["esp32_temp"] = temperatureRead();

    // Bluetooth stats
    doc["bt"]["connected"] = btConnected;
    doc["bt"]["paired"] = isPaired;
    doc["bt"]["status"] = lastBtStatus;
    doc["bt"]["target_address"] = SOUNDBAR_ADDRESS;
    doc["bt"]["connect_attempts"] = btStats.connectAttempts;
    doc["bt"]["connect_successes"] = btStats.connectSuccesses;
    doc["bt"]["connect_failures"] = btStats.connectFailures;
    doc["bt"]["disconnects"] = btStats.disconnects;
    doc["bt"]["last_connect_duration_ms"] = btStats.lastConnectDuration;
    doc["bt"]["total_connected_time_ms"] = btStats.totalConnectedTime +
        (btConnected ? (millis() - btStats.connectedSince) : 0);
    doc["bt"]["bytes_sent"] = btStats.bytesSent;
    doc["bt"]["bytes_received"] = btStats.bytesReceived;
    doc["bt"]["last_error"] = btStats.lastError;

    if (btStats.connectAttempts > 0) {
        doc["bt"]["success_rate"] = 100.0 * btStats.connectSuccesses / btStats.connectAttempts;
    }

    // MQTT info
    doc["mqtt"]["connected"] = mqtt.connected();
    doc["mqtt"]["host"] = MQTT_HOST;
    doc["mqtt"]["port"] = MQTT_PORT;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// GET /reset_pairing - Reset BT pairing
void handleResetPairing() {
    if (!checkAuth()) return;

    DBG("HTTP: Reset pairing requested");
    resetPairing();

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Pairing reset. Put soundbar in pairing mode. Will reconnect in 30 seconds (or call /reconnect).";

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// GET /reconnect - Trigger immediate reconnect
void handleReconnect() {
    if (!checkAuth()) return;

    DBG("HTTP: Reconnect requested");

    // Clear hold-off and trigger immediate reconnect
    reconnectHoldOffUntil = 0;
    lastBtConnectAttempt = 0;

    JsonDocument doc;
    doc["success"] = true;
    doc["message"] = "Reconnect triggered";

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// GET /send_command - Send command to soundbar
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

// 404 handler
void handleNotFound() {
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
}
