#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include "yas_commands.h"

// Initialize MQTT client
void initMqtt();

// Connection management
void connectMqtt();

// MQTT callback
void mqttCallback(char* topic, byte* payload, unsigned int length);

// Publishing
void publishBtStatus();
void publishStatus(const YasStatus& status);
void publishDiscovery();

// Volume/Subwoofer control (called from MQTT)
void setVolume(int targetVolume);
void setSubwoofer(int targetSubwoofer);

#endif
