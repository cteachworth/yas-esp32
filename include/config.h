#ifndef CONFIG_H
#define CONFIG_H

// Include secrets (copy secrets.h.example to secrets.h and edit)
#include "secrets.h"

// HTTP Server Configuration
#define HTTP_PORT 80

// Bluetooth device name (how this device appears to others)
#define BT_DEVICE_NAME "YAS-Bridge"

// Connection settings
#define BT_RECONNECT_DELAY_MS 10000
#define STATUS_REQUEST_TIMEOUT_MS 3000
#define WIFI_RECONNECT_DELAY_MS 5000
#define MQTT_RECONNECT_DELAY_MS 5000

// MQTT Topics
#define MQTT_BASE_TOPIC "homeassistant/soundbar"
#define MQTT_STATE_TOPIC MQTT_BASE_TOPIC "/state"
#define MQTT_COMMAND_TOPIC MQTT_BASE_TOPIC "/command"
#define MQTT_VOLUME_TOPIC MQTT_BASE_TOPIC "/set_volume"
#define MQTT_SUBWOOFER_TOPIC MQTT_BASE_TOPIC "/set_subwoofer"
#define MQTT_AVAILABLE_TOPIC MQTT_BASE_TOPIC "/available"

// Status polling interval (for catching remote control changes)
#define STATUS_POLL_INTERVAL_MS 2000

#endif
