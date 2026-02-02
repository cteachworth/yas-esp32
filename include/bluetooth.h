#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <Arduino.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include "yas_commands.h"

// Initialize Bluetooth with SSP
void initBluetooth();

// Connection management
void connectBluetooth();
void resetPairing();

// Callbacks for ESP-IDF
void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param);

// Command interface
bool sendCommand(const String& cmd);
YasStatus requestStatus();

#endif
