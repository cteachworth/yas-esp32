#ifndef CONFIG_H
#define CONFIG_H

// Include secrets (copy secrets.h.example to secrets.h and edit)
#include "secrets.h"

// HTTP Server Configuration
#define HTTP_PORT 80

// Bluetooth device name (how this device appears to others)
#define BT_DEVICE_NAME "YAS-Bridge"

// Connection timing settings
#define BT_RECONNECT_DELAY_MS 10000       // 10s between reconnect attempts
#define STATUS_REQUEST_TIMEOUT_MS 3000    // 3s timeout for status responses
#define WIFI_RECONNECT_DELAY_MS 5000      // 5s between WiFi reconnect attempts
#define MQTT_RECONNECT_DELAY_MS 5000      // 5s between MQTT reconnect attempts

// Status polling interval (for catching remote control changes)
#define STATUS_POLL_INTERVAL_MS 5000      // Poll every 5 seconds

// MQTT Topics
#define MQTT_BASE_TOPIC "homeassistant/soundbar"
#define MQTT_STATE_TOPIC MQTT_BASE_TOPIC "/state"
#define MQTT_COMMAND_TOPIC MQTT_BASE_TOPIC "/command"
#define MQTT_VOLUME_TOPIC MQTT_BASE_TOPIC "/set_volume"
#define MQTT_SUBWOOFER_TOPIC MQTT_BASE_TOPIC "/set_subwoofer"
#define MQTT_AVAILABLE_TOPIC MQTT_BASE_TOPIC "/available"
#define MQTT_RESTART_TOPIC MQTT_BASE_TOPIC "/restart"
#define MQTT_RESET_PAIRING_TOPIC MQTT_BASE_TOPIC "/reset_pairing"

#endif
