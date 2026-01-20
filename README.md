# YAS Bluetooth Bridge for ESP32

ESP32 firmware that bridges Yamaha YAS soundbar control via Bluetooth Classic to HTTP and MQTT, for integration with Home Assistant.

## Features

- **MQTT with Home Assistant Discovery** - Entities auto-appear in HA
- **HTTP API** - RESTful control and status
- **Real-time sync** - Polls soundbar every 2 seconds to catch remote control changes
- **Optimistic updates** - UI responds immediately

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
#define SOUNDBAR_NAME "ATS-1070 Yamaha"
#define SOUNDBAR_ADDRESS "00:1A:7D:XX:XX:XX"

// MQTT (use IP address if DNS doesn't resolve)
#define MQTT_HOST "192.168.1.100"
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""

// Optional HTTP auth
#define API_KEY ""
```

### 3. Find Your Soundbar's Bluetooth Info

On Linux/Raspberry Pi:
```bash
bluetoothctl
> scan on
# Look for YAS-207, ATS-1070, or similar
> quit
```

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
| `number.yas_soundbar_subwoofer` | Number | Subwoofer level (0-30) |
| `select.yas_soundbar_input` | Select | Input source |
| `select.yas_soundbar_surround` | Select | Surround mode |

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
```

### HTTP API

The HTTP API is still available for direct control or as a fallback.

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

**GET /send_command?command=\<cmd\>** - Send command

#### Commands

| Category | Commands |
|----------|----------|
| Power | `power_on`, `power_off`, `power_toggle` |
| Input | `set_input_hdmi`, `set_input_analog`, `set_input_bluetooth`, `set_input_tv` |
| Surround | `set_surround_3d`, `set_surround_tv`, `set_surround_stereo`, `set_surround_movie`, `set_surround_music`, `set_surround_sports`, `set_surround_game` |
| Volume | `volume_up`, `volume_down`, `mute_on`, `mute_off`, `mute_toggle` |
| Subwoofer | `subwoofer_up`, `subwoofer_down` |
| Audio | `clearvoice_on`, `clearvoice_off`, `clearvoice_toggle`, `bass_ext_on`, `bass_ext_off`, `bass_ext_toggle` |

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
| `homeassistant/soundbar/set_subwoofer` | Subscribe | Target subwoofer (0-30) |
| `homeassistant/soundbar/available` | Publish | `online` or `offline` |

## Troubleshooting

### Bluetooth won't connect
- Soundbar must be powered on
- Verify Bluetooth name/address in secrets.h
- Ensure nothing else is connected to the soundbar
- ESP32 must be within Bluetooth range (~10m)

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

## License

Based on work by [Paul Bottein](https://github.com/piitaya/yas-207-bridge).
