#include "bluetooth.h"
#include "state.h"
#include "config.h"
#include "debug.h"
#include "yas_commands.h"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>

// GAP callback for SSP (Secure Simple Pairing) events
void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                DBG("BT GAP: Authentication SUCCESS! Device: %s", param->auth_cmpl.device_name);
                DBG("BT GAP: Link key stored, fast reconnect should work now");
            } else {
                DBG("BT GAP: Authentication FAILED, status: %d", param->auth_cmpl.stat);
            }
            break;
        case ESP_BT_GAP_PIN_REQ_EVT:
            DBG("BT GAP: Legacy PIN request - responding with 1234");
            {
                esp_bt_pin_code_t pin = {'1', '2', '3', '4'};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
            }
            break;
        case ESP_BT_GAP_CFM_REQ_EVT:
            DBG("BT GAP: SSP User Confirmation request, passkey: %06d", param->cfm_req.num_val);
            DBG("BT GAP: Auto-confirming for Just Works mode...");
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
        case ESP_BT_GAP_KEY_NOTIF_EVT:
            DBG("BT GAP: Passkey notification: %06d", param->key_notif.passkey);
            break;
        case ESP_BT_GAP_KEY_REQ_EVT:
            DBG("BT GAP: Passkey request");
            break;
        case ESP_BT_GAP_MODE_CHG_EVT:
            DBG("BT GAP: Mode change, mode: %d", param->mode_chg.mode);
            break;
        case ESP_BT_GAP_DISC_RES_EVT:
            {
                char bda_str[18];
                snprintf(bda_str, sizeof(bda_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                    param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                    param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5]);

                // Look for device name in properties
                const char* dev_name = "unknown";
                for (int i = 0; i < param->disc_res.num_prop; i++) {
                    if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME) {
                        dev_name = (const char*)param->disc_res.prop[i].val;
                        break;
                    }
                }
                DBG("BT GAP: Discovered: %s [%s]", dev_name, bda_str);
            }
            break;
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            DBG("BT GAP: Discovery %s", param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED ? "STARTED" : "STOPPED");
            break;
        default:
            DBG("BT GAP: Event %d", event);
            break;
    }
}

// SPP callback for connection events (minimal logging - key events only)
void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    switch (event) {
        case ESP_SPP_INIT_EVT:
            DBG("BT SPP: Initialized");
            break;
        case ESP_SPP_OPEN_EVT:
            DBG("BT SPP: Connected (handle=%d)", param->open.handle);
            break;
        case ESP_SPP_CLOSE_EVT:
            DBG("BT SPP: Disconnected");
            break;
        default:
            break;
    }
}

// Initialize Bluetooth with SSP
void initBluetooth() {
    DBG("BT: Initializing BluetoothSerial as master...");

    if (!SerialBT.begin(BT_DEVICE_NAME, true)) {
        DBG("BT: Initialization FAILED!");
        delay(1000);
        ESP.restart();
    }
    DBG("BT: Initialized as '%s'", BT_DEVICE_NAME);

    // Register GAP callback for SSP events
    esp_bt_gap_register_callback(gapCallback);
    DBG("BT: GAP callback registered");

    // Set IO capability to NoInputNoOutput for "Just Works" pairing
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, sizeof(iocap));
    DBG("BT: IO capability set to NoInputNoOutput (Just Works)");

    // Enable Secure Simple Pairing
    SerialBT.enableSSP();
    DBG("BT: SSP enabled");

    // Register SPP callback
    SerialBT.register_callback(btCallback);
    DBG("BT: Callback registered");

    // Check for existing bonded devices
    int bondedCount = esp_bt_gap_get_bond_device_num();
    DBG("BT: Bonded devices in NVS: %d", bondedCount);

    if (bondedCount > 0) {
        esp_bd_addr_t *bondedList = (esp_bd_addr_t *)malloc(bondedCount * sizeof(esp_bd_addr_t));
        if (bondedList && esp_bt_gap_get_bond_device_list(&bondedCount, bondedList) == ESP_OK) {
            for (int i = 0; i < bondedCount; i++) {
                DBG("BT: Bonded[%d]: %02x:%02x:%02x:%02x:%02x:%02x", i,
                    bondedList[i][0], bondedList[i][1], bondedList[i][2],
                    bondedList[i][3], bondedList[i][4], bondedList[i][5]);
            }
            free(bondedList);
        }
    }

    DBG("BT: Target soundbar: %s", SOUNDBAR_NAME);
    DBG("BT: Target address: %s", SOUNDBAR_ADDRESS);
}

// Reset Bluetooth pairing - clears bond and prepares for fresh SSP handshake
void resetPairing() {
    DBG("BT: Resetting pairing...");

    // Clear pairing state from NVS
    isPaired = false;
    prefs.putBool("paired", false);

    // Remove bonded device from ESP-IDF's BT stack
    uint8_t addr[6];
    if (sscanf(SOUNDBAR_ADDRESS, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) == 6) {
        esp_err_t err = esp_bt_gap_remove_bond_device(addr);
        DBG("BT: Removed bond device, result: %s", esp_err_to_name(err));
    }

    // Disconnect if connected
    if (btConnected) {
        SerialBT.disconnect();
        btConnected = false;
    }

    // Hold off reconnection for 30 seconds
    reconnectHoldOffUntil = millis() + 30000;
    setBtStatus("pairing_reset");
    DBG("BT: Pairing reset - will reconnect in 30 seconds");
}

// Connect to soundbar
void connectBluetooth() {
    lastBtConnectAttempt = millis();
    btStats.connectAttempts++;

    if (SerialBT.connected()) {
        DBG("BT: Already connected");
        btConnected = true;
        return;
    }

    DBG("========================================");
    DBG("BT: Connection attempt #%lu", btStats.connectAttempts);
    DBG("BT: Target: \"%s\"", SOUNDBAR_NAME);
    DBG("BT: Free heap: %d bytes", ESP.getFreeHeap());

    // Ensure we're fully disconnected before trying
    if (SerialBT.hasClient()) {
        DBG("BT: Has stale client, disconnecting...");
        SerialBT.disconnect();
        delay(500);
    }

    setBtStatus("connecting");

    unsigned long connectStart = millis();
    bool connected = false;

    // Always try MAC address first if configured (more reliable, skips discovery)
    if (strlen(SOUNDBAR_ADDRESS) > 0) {
        uint8_t addr[6];
        if (sscanf(SOUNDBAR_ADDRESS, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) == 6) {

            // Try up to 3 rapid attempts - some devices need a "wake up" connection
            for (int attempt = 1; attempt <= 3 && !connected; attempt++) {
                DBG("BT: MAC connect attempt %d/3: %s", attempt, SOUNDBAR_ADDRESS);
                connected = SerialBT.connect(addr);
                if (connected) {
                    DBG("BT: MAC connect succeeded on attempt %d!", attempt);
                } else if (attempt < 3) {
                    DBG("BT: Attempt %d failed, retrying in 2s...", attempt);
                    delay(2000);
                }
            }

            if (!connected) {
                DBG("BT: All MAC connect attempts failed, trying by name...");
            }
        }
    }

    // If not connected yet, try by name
    if (!connected) {
        DBG("BT: Connecting by name: \"%s\"", SOUNDBAR_NAME);
        connected = SerialBT.connect(SOUNDBAR_NAME);
    }

    unsigned long connectDuration = millis() - connectStart;
    btStats.lastConnectDuration = connectDuration;

    if (connected) {
        btStats.connectSuccesses++;
        btStats.connectedSince = millis();
        btConnected = true;

        DBG("BT: SUCCESS! Connected in %lu ms", connectDuration);
        DBG("BT: Success rate: %lu/%lu (%.1f%%)",
            btStats.connectSuccesses, btStats.connectAttempts,
            100.0 * btStats.connectSuccesses / btStats.connectAttempts);

        // Save paired state
        if (!isPaired) {
            isPaired = true;
            prefs.putBool("paired", true);
            DBG("BT: Saved paired state to NVS");
        }

        setBtStatus("connected");

        if (mqtt.connected()) {
            mqtt.publish(MQTT_AVAILABLE_TOPIC, "online", true);
        }
    } else {
        btStats.connectFailures++;
        btConnected = false;

        DBG("BT: FAILED after %lu ms", connectDuration);
        DBG("BT: Failure rate: %lu/%lu (%.1f%%)",
            btStats.connectFailures, btStats.connectAttempts,
            100.0 * btStats.connectFailures / btStats.connectAttempts);

        setBtStatus("connect_failed", String("attempt_") + String(btStats.connectAttempts));

        if (mqtt.connected()) {
            mqtt.publish(MQTT_AVAILABLE_TOPIC, "offline", true);
        }
    }

    DBG("BT: Next attempt in %d ms", BT_RECONNECT_DELAY_MS);
    DBG("----------------------------------------");
}

// Send command to soundbar
bool sendCommand(const String& cmd) {
    String encoded = encodeCommand(cmd);
    if (encoded.length() == 0) {
        DBG("CMD: Unknown command: %s", cmd.c_str());
        return false;
    }

    uint8_t buffer[32];
    int len = hexStringToBytes(encoded, buffer, sizeof(buffer));

    DBG("CMD TX: %s -> [%s] (%d bytes)", cmd.c_str(), bytesToHex(buffer, len).c_str(), len);

    size_t written = SerialBT.write(buffer, len);
    btStats.bytesSent += written;

    if (written != len) {
        DBG("CMD: Write failed, sent %d of %d bytes", written, len);
        return false;
    }

    return true;
}

// Request and parse status from soundbar
YasStatus requestStatus() {
    YasStatus status = {false, "unknown", false, 0, 0, "unknown", false, false, false};

    // Flush any stale data
    int flushed = 0;
    while (SerialBT.available()) {
        SerialBT.read();
        flushed++;
    }
    if (flushed > 0) {
        DBG("STATUS: Flushed %d stale bytes", flushed);
    }

    if (!sendCommand("report_status")) {
        DBG("STATUS: Failed to send request");
        return status;
    }

    // Wait for response with timeout
    unsigned long requestStart = millis();
    unsigned long lastByteTime = millis();
    uint8_t buffer[64];
    int len = 0;

    while (millis() - requestStart < STATUS_REQUEST_TIMEOUT_MS && len < (int)sizeof(buffer)) {
        if (SerialBT.available()) {
            buffer[len++] = SerialBT.read();
            lastByteTime = millis();
            btStats.bytesReceived++;
        } else if (len > 0 && millis() - lastByteTime > 100) {
            break;
        }
        delay(1);
    }

    if (len > 0) {
        DBG("STATUS RX: [%s] (%d bytes in %lu ms)",
            bytesToHex(buffer, len).c_str(), len, millis() - requestStart);

        String response = bytesToHexString(buffer, len);
        status = decodeStatus(response);

        if (status.valid) {
            DBG("STATUS: power=%s input=%s vol=%d mute=%s surround=%s",
                status.power ? "ON" : "OFF",
                status.input.c_str(),
                status.volume,
                status.muted ? "ON" : "OFF",
                status.surround.c_str());
        } else {
            DBG("STATUS: Failed to decode response");
        }
    } else {
        DBG("STATUS: No response (timeout after %lu ms)", millis() - requestStart);
    }

    return status;
}
