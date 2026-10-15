# ESPSpotifyOS

> **Disclaimer**: This is an **unofficial, hobby project** built for **personal use only**. It is **not affiliated with, endorsed by, or connected to Spotify** or any other streaming service. Do not use it for commercial purposes.

An ESP-IDF application for ESP32-S3 that pairs with a small media server to play your own music library over AUX (PCM5102).

## Mission

A personal Spotify companion player. The idea is simple:

1. **Import** - point the companion server at your Spotify library (e.g. liked tracks) to know what you want to play.
2. **Fetch** - the server finds matching audio for each track via **yt-dlp** and stores it locally.
3. **Stream** - the server acts as a **media streaming layer**, serving the imported audio back to the ESP32 over Wi-Fi so the device plays your library through AUX / a speaker.

In short: your Spotify library drives the music, the media server handles the heavy lifting (downloading and streaming), and the ESP32-S3 handles playback and the OLED UI.

## Current State

The project targets a **custom ESP32-S3 PCB** with SSD1306 OLED, five switches, SPI microSD, and a PCM5102 AUX DAC.

### Hardware pins

I2C Display (SSD1306 128x64):
- SDA: IO6
- SCL: IO8

SPI SD Card:
- MOSI: IO11
- MISO: IO12
- SCK:  IO14
- CS:   IO21
- CD:   IO47

Switches:
- SWC (Center): IO4
- SWB (Bottom): IO15
- SWT (Top):    IO16
- SWL (Left):   IO18
- SWR (Right):  IO17

AUX (PCM5102 I2S):
- SCK (MCLK): IO42
- BCK:  IO41
- DIN:  IO40
- LRCK: IO39
- DET:  IO38 (reserved)

### What works

- **Display / UI**: SSD1306 over I2C + LVGL 9. Five tabs cycled with a **short press on Center**: Setup, Library, Import, Settings, Player.
- **Switches**: debounced GPIO driver (short vs long Center).
- **Wi-Fi**: station-mode auto-connect with reconnect handling. Credentials are seeded from Kconfig on first boot and persisted in NVS.
- **Wi-Fi provisioning AP**: if the device cannot connect, it starts a provisioning access point (`ESPSpotifyOS-Setup` by default) and serves a settings page to change the Wi-Fi network and restart. Wi-Fi SSID/password entry stays in the browser (`settings.html`) — not on the OLED.
- **Spotify login**: embedded web server on port `8080` serves a login page; full OAuth Authorization Code flow with token persistence and automatic token refresh. OLED Setup tab shows the `/submit` URL when logged out.
- **Library tab**: media-server tracks + SD-cached files; Right plays the selection over AUX.
- **Import tab**: Spotify liked tracks; Right queues `transfer_manager_import`.
- **Player tab**: now-playing, play/pause, next/prev, volume.
- **Settings tab**: shows IP + volume; Right (twice) logs out of Spotify. Wi-Fi still via `/settings`.
- **Playback engine**: Server tracks via HTTP streaming (HTTP Range resume) and SD tracks from the cache, decoded with `esp_audio_codec`.
- **Audio output**: **AUX only** — external I2S DAC (PCM5102) on the pins above. Onboard DAC / amp paths from older boards are disabled.
- **SD cache / offline**: FatFS over SPI2; imported tracks can be cached under `/music`.
- **Media server (backend)**: track search/download via `yt-dlp`, SQLite metadata store, MP3 storage, paginated listing, query/URL import, and a streaming endpoint with HTTP Range support. See [`server/`](server/README.md).

### Out of scope (for now)

- **Bluetooth audio**: ESP32-S3 has BLE only (no Classic BT / A2DP). Pairing to BT speakers/headphones is deferred.
- On-device Wi-Fi password typing on the OLED.
- Album art / seek polish.

### What's next

- Playback polish: seeking, album art, queue that spans pages.
- Optional external BT audio module (or a Classic-ESP32 variant) if wireless speakers are needed.
- AUX DET jack-sense auto-routing.

---

## Hardware Requirements

- **Board**: ESP32-S3 custom PCB (or compatible pinout)
- **Display**: SSD1306 OLED 128x64 (I2C)
- **Audio**: PCM5102 (or compatible) I2S DAC on the AUX connector
- **Input**: five tactile switches (Up / Down / Left / Right / Center)
- **Optional**: microSD card for offline cache

---

## Project Structure

```
ESPSpotifyOS/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── main/
│   ├── core/
│   │   └── time_sync.c/.h           # SNTP time sync (needed for token refresh)
│   ├── connectivity/
│   │   └── wifi.c/.h               # Wi-Fi station management
│   ├── data/
│   │   ├── auth_state.c/.h         # Spotify tokens/expiry/login (NVS "auth")
│   │   ├── device_state.c/.h       # wifi creds + screen/tab (NVS "device")
│   │   └── state.c/.h              # state_init() + legacy blob migration
│   ├── display/
│   │   └── display.c/.h            # SSD1306 + LVGL port
│   ├── input/
│   │   └── buttons.c/.h            # five-switch debounce driver
│   ├── ui/
│   │   └── ui.c/.h                 # tab controller + OLED screens
│   ├── net/
│   │   ├── http_client.c/.h        # HTTP client wrapper (GET/POST/stream)
│   │   ├── media_client.c/.h       # companion media server client
│   │   ├── spotify_auth.c/.h       # Spotify OAuth + token lifecycle
│   │   └── spotify_api.c/.h        # Spotify Web API (profile, saved tracks)
│   ├── server/
│   │   ├── webserver.c/.h          # embedded HTTP server lifecycle + route registration
│   │   └── routes.c/.h             # HTTP endpoints (/submit OAuth, /settings, /wifi-scan)
│   ├── services/
│   │   ├── audio_output.c/.h       # I2S AUX output + software volume
│   │   ├── player.c/.h             # playback engine (decode/stream/queue)
│   │   ├── track_cache.c/.h        # streams tracks to the SD cache
│   │   └── transfer_manager.c/.h   # background import/download worker
│   ├── storage/
│   │   └── sd_storage.c/.h         # SD (FatFS) mount + music directory scan
│   ├── utils/
│   │   └── storage.c/.h            # NVS blob helpers
│   ├── web/
│   │   ├── settings.html           # Wi-Fi settings page (served at /settings)
│   │   └── submit.html             # Spotify OAuth login page
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   ├── idf_component.yml
│   └── main.c                      # Application entry point
└── server/                         # Go media service (see server/README.md)
    ├── cmd/                        # entry (server), test (smoke binary)
    └── internal/                   # config, database, handlers, routes, services, types
```

---

## Getting Started

### 1. Prerequisites

- [ESP-IDF v5.x / v6.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) installed and sourced.
- Target: `idf.py set-target esp32s3`

### 2. Configure the Project

Open the configuration menu:
```bash
idf.py menuconfig
```

Configure Wi-Fi and Spotify credentials:
- `CONFIG_ESP_WIFI_SSID` / `CONFIG_ESP_WIFI_PASSWORD`: your Wi-Fi network credentials
- `CONFIG_PLAYER_SPOTIFY_CLIENT_ID` / `CONFIG_PLAYER_SPOTIFY_CLIENT_SECRET`: your Spotify app credentials (from the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard))
- `CONFIG_PLAYER_MEDIA_SERVICE_URL` / `CONFIG_PLAYER_MEDIA_API_KEY`: the media server's address (e.g. `http://192.168.1.50:8081`) and the `SECRET` value from its `.env` file. Needed for the import feature.
- `CONFIG_PLAYER_AP_SSID` / `CONFIG_PLAYER_AP_PASSWORD`: the provisioning access point (see below). The AP password must be at least 8 characters, or empty for an open AP.
- `PLAYER_AUDIO_OUTPUT`: default **External I2S DAC (PCM5102 AUX)** with the AUX pin defaults above.
- `CONFIG_PLAYER_TEST_TONE`: play a short 440 Hz tone at boot to verify AUX without streaming. Turn it off once audio works.

### 3. Build and Flash

Build, flash, and open the serial monitor:
```bash
idf.py -p <PORT> flash monitor
```
*(To exit the monitor, press `Ctrl + ]`)*

### OLED controls (quick reference)

| Input | Action |
|-------|--------|
| Center | Next tab (Setup → Library → Import → Settings → Player). Blocked while Wi-Fi or Spotify login is missing |
| Hold Center | Play / pause (any tab) |
| Up / Down | Move in lists; volume in Player and Settings |
| Right | Action: play (Library), import (Import), logout (Settings, press twice), next track (Player) |
| Left | Previous page (lists), cancel (Settings), previous track (Player) |

---

## Wi-Fi Provisioning

On boot the device tries to join the configured network. If that fails, it
starts a provisioning access point instead and shows the AP name plus the setup
URL on the OLED:

1. Join the `ESPSpotifyOS-Setup` access point (password from
   `CONFIG_PLAYER_AP_PASSWORD`, default `spotify-setup`).
2. Open `http://192.168.4.1:8080/settings` in a browser.
3. Pick a nearby network from the dropdown (or type the SSID), enter the
   password, and save. Empty password = open network.
4. The device saves the credentials to NVS and restarts, then connects to the
   new network.

The **Reset to Defaults** button restores the build-time Kconfig credentials.
The same settings page is also reachable at `http://<ESP32_IP>:8080/settings`
while the device is connected, so you can switch networks without the AP.

---

## Web Server Endpoints

The ESP32 starts an HTTP web server on port `8080` in both station and
provisioning-AP mode:

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/submit` | Login page - builds a Spotify OAuth URL and lets you paste the authorization code back |
| `POST` | `/submit` | Accepts `spotify_code=<code>`, exchanges it for tokens, and stores them |
| `GET` | `/settings` | Wi-Fi settings page |
| `POST` | `/settings` | Accepts `ssid=<ssid>&password=<password>`, saves them, and restarts |
| `POST` | `/settings/reset` | Restores the default Wi-Fi credentials and restarts |
| `GET` | `/wifi-scan` | JSON list of nearby networks (provisioning-AP mode only) |

Example access from a browser or terminal:
```bash
curl -v http://<ESP32_IP>:8080/submit
curl -v http://192.168.4.1:8080/settings
```
