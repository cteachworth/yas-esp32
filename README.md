# YAS Bluetooth Bridge for ESP32

ESP32 firmware that bridges Yamaha YAS soundbar control via Bluetooth Classic to HTTP and MQTT, for integration with Home Assistant.

## Features

- **MQTT with Home Assistant Discovery** - Entities auto-appear in HA
- **HTTP API** - RESTful control and status
- **Real-time sync** - Polls soundbar every 5 seconds to catch remote control changes
- **SSP Pairing** - Secure Simple Pairing with fast reconnect (~1.7s after initial pairing)
- **Debug endpoint** - Connection stats and diagnostics at `/debug`

## Requirements

- **ESP32 board** (NOT ESP32-S2, S3, or C3 - these don't support Bluetooth Classic)
- PlatformIO (VSCode extension or CLI)
- Yamaha YAS soundbar (tested with YAS-207, YAS-1070)

## Setup

### 1. Install PlatformIO

Install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for VSCode, or install the CLI:

```bash
pip install platformio
```

### 2. Configure

Copy the secrets template and edit with your credentials:

```bash
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h`:

```cpp
// WiFi
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// Soundbar Bluetooth
#define SOUNDBAR_NAME "ATS-1070 Yamaha"      // Exact Bluetooth name
#define SOUNDBAR_ADDRESS "c8:84:47:40:ec:3c" // MAC address (discovered on first boot)

// MQTT (use IP address if DNS doesn't resolve)
#define MQTT_HOST "192.168.1.100"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""

// Optional HTTP auth
#define API_KEY ""
```

### 3. First Boot - Pairing

On first boot, the ESP32 needs to pair with your soundbar:

1. **Put soundbar in pairing mode** (usually hold Bluetooth button until LED flashes)
2. **Power on ESP32** - it will connect by name and establish SSP pairing
3. **Note the MAC address** in serial output - update `SOUNDBAR_ADDRESS` in secrets.h

After initial pairing:
- Soundbar remembers the ESP32
- ESP32 uses fast MAC-based reconnect (~1.7s vs ~4s)
- No need to put soundbar in pairing mode again

### 4. Build and Upload

```bash
pio run                    # Build
pio run --target upload    # Upload
pio device monitor         # Monitor serial output
```

## Home Assistant Integration

### MQTT (Recommended)

With MQTT configured, entities are automatically discovered:

| Entity | Type | Description |
|--------|------|-------------|
| `switch.yas_soundbar_power` | Switch | Power on/off |
| `switch.yas_soundbar_mute` | Switch | Mute on/off |
| `switch.yas_soundbar_clear_voice` | Switch | Clear voice enhancement |
| `switch.yas_soundbar_bass_ext` | Switch | Bass extension |
| `number.yas_soundbar_volume` | Number | Volume (0-50) |
| `number.yas_soundbar_subwoofer` | Number | Subwoofer level (0-32) |
| `select.yas_soundbar_input` | Select | Input source |
| `select.yas_soundbar_surround` | Select | Surround mode |
| `sensor.yas_soundbar_temperature` | Sensor | ESP32 internal temperature |
| `sensor.yas_soundbar_bt_status` | Sensor | Bluetooth connection status |
| `button.yas_soundbar_restart` | Button | Restart the ESP32 bridge |
| `button.yas_soundbar_reset_pairing` | Button | Clear BT pairing and reconnect |

Example Lovelace card:

```yaml
type: entities
title: Soundbar
entities:
  - entity: switch.yas_soundbar_power
  - entity: number.yas_soundbar_volume
  - entity: select.yas_soundbar_input
  - entity: select.yas_soundbar_surround
  - entity: switch.yas_soundbar_mute
  - entity: number.yas_soundbar_subwoofer
  - entity: switch.yas_soundbar_clear_voice
  - entity: switch.yas_soundbar_bass_ext
  - entity: sensor.yas_soundbar_bt_status
```

### HTTP API

The HTTP API is available for direct control or as a fallback.

#### Endpoints

**GET /** - Bridge info and connection status

**GET /status** - Current soundbar state:
```json
{
  "power": true,
  "input": "hdmi",
  "muted": false,
  "volume": 25,
  "subwoofer": 32,
  "surround": "3d",
  "bass_ext": false,
  "clear_voice": false
}
```

**GET /debug** - Connection statistics and diagnostics:
```json
{
  "uptime_ms": 123456,
  "free_heap": 75000,
  "wifi_rssi": -65,
  "esp32_temp": 45.5,
  "bt": {
    "connected": true,
    "paired": true,
    "status": "connected",
    "connect_attempts": 1,
    "connect_successes": 1,
    "last_connect_duration_ms": 1736
  }
}
```

**GET /reset_pairing** - Clear Bluetooth bond and trigger re-pairing (30s cooldown)

**GET /reconnect** - Force immediate Bluetooth reconnection attempt

**GET /send_command?command=\<cmd\>** - Send command

#### Commands

| Category | Commands |
|----------|----------|
| Power | `power_on`, `power_off`, `power_toggle` |
| Input | `set_input_hdmi`, `set_input_analog`, `set_input_bluetooth`, `set_input_tv` |
| Surround | `set_surround_3d`, `set_surround_tv`, `set_surround_stereo`, `set_surround_movie`, `set_surround_music`, `set_surround_sports`, `set_surround_game`, `surround_toggle` |
| Volume | `volume_up`, `volume_down`, `mute_on`, `mute_off`, `mute_toggle` |
| Subwoofer | `subwoofer_up`, `subwoofer_down` |
| Audio | `clearvoice_on`, `clearvoice_off`, `clearvoice_toggle`, `bass_ext_on`, `bass_ext_off`, `bass_ext_toggle` |
| Other | `bluetooth_standby_toggle`, `dimmer` |

#### Authentication

If `API_KEY` is set, include it via:
- Header: `Authorization: Bearer <api_key>`
- Query: `?api_key=<api_key>`

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `homeassistant/soundbar/state` | Publish | JSON state object |
| `homeassistant/soundbar/command` | Subscribe | Command name |
| `homeassistant/soundbar/set_volume` | Subscribe | Target volume (0-50) |
| `homeassistant/soundbar/set_subwoofer` | Subscribe | Target subwoofer (0-32) |
| `homeassistant/soundbar/available` | Publish | `online` or `offline` |
| `homeassistant/soundbar/bt_status` | Publish | Bluetooth status |
| `homeassistant/soundbar/temperature` | Publish | ESP32 temperature |
| `homeassistant/soundbar/restart` | Subscribe | Send any message to restart |
| `homeassistant/soundbar/reset_pairing` | Subscribe | Send any message to reset BT pairing |

## Troubleshooting

### Bluetooth won't connect on first boot
- **Put soundbar in pairing mode first** - LED should be flashing
- Verify `SOUNDBAR_NAME` matches exactly (check serial output)
- Ensure no other device is connected to the soundbar

### Bluetooth won't reconnect after reboot
- Check `/debug` endpoint for connection stats
- If MAC address is wrong, the fast connect will fail (falls back to name)
- Clear NVS to reset pairing: in code set `prefs.putBool("paired", false)`

### Connection drops frequently
- Check WiFi signal strength at `/debug` (wifi_rssi)
- ESP32 temperature - high temps can cause instability
- Soundbar may have power saving that disconnects idle connections

### MQTT won't connect
- Use IP address instead of hostname if DNS fails
- Check MQTT credentials
- Verify MQTT broker is running

### Build errors about Bluetooth Classic
- Use standard ESP32, not ESP32-S2/S3/C3 (no Bluetooth Classic)

### WiFi won't connect
- Check credentials in secrets.h
- ESP32 only supports 2.4GHz WiFi

### Entities not appearing in Home Assistant
- Check MQTT broker connection in serial monitor
- Restart Home Assistant after first connection
- Look in Settings > Devices > MQTT for "YAS Soundbar"

## Serial Output

On boot you'll see detailed connection info:

```
========================================
  YAS Bluetooth Bridge v2.2.0
========================================
[00:02.782] BT: Paired state from NVS: YES
[00:03.231] BT: SSP enabled (Just Works mode)
[00:03.277] BT: Trying fast connect by MAC: 12:34:56:78:9a:bc
[00:05.012] BT: Fast MAC connect succeeded!
[00:05.012] BT: SUCCESS! Connected in 1736 ms
```

## Acknowledgments

Based on work by [Paul Bottein](https://github.com/piitaya/yas-207-bridge).
