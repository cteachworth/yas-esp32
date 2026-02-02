#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include "yas_commands.h"

// Bluetooth statistics
struct BtStats {
    unsigned long connectAttempts = 0;
    unsigned long connectSuccesses = 0;
    unsigned long connectFailures = 0;
    unsigned long disconnects = 0;
    unsigned long lastConnectDuration = 0;
    unsigned long totalConnectedTime = 0;
    unsigned long connectedSince = 0;
    unsigned long bytesSent = 0;
    unsigned long bytesReceived = 0;
    String lastError;
};

// Global objects (defined in main.cpp)
extern BluetoothSerial SerialBT;
extern WebServer server;
extern PubSubClient mqtt;
extern Preferences prefs;

// Connection state (defined in main.cpp)
extern BtStats btStats;
extern bool isPaired;
extern bool btConnected;
extern unsigned long lastBtConnectAttempt;
extern unsigned long reconnectHoldOffUntil;

// Status tracking (defined in main.cpp)
extern String lastBtStatus;
extern String lastPublishedBtStatus;
extern YasStatus lastSoundbarStatus;

// Status helper
void setBtStatus(const String& status, const String& detail = "");

#endif
