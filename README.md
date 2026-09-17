# ESPSpotifyOS

> **Disclaimer**: This is an **unofficial, hobby project** built for **personal use only**. It is **not affiliated with, endorsed by, or connected to Spotify** or any other streaming service. Do not use it for commercial purposes.

An ESP-IDF application for ESP32 that pairs with a small media server to turn your own music library into a physical, touchscreen player.

## Mission

A personal Spotify companion player. The idea is simple:

1. **Import** — point the companion server at your Spotify library (e.g. liked tracks) to know what you want to play.
2. **Fetch** — the server finds matching audio for each track via **yt-dlp** and stores it locally.
3. **Stream** — the server acts as a **media streaming layer**, serving the imported audio back to the ESP32 over Wi-Fi so the device plays your library through a speaker.

In short: your Spotify library drives the music, the media server handles the heavy lifting (downloading and streaming), and the ESP32 is the physical player you interact with on a touchscreen.

## Current State

The project is a work in progress. Here's where it stands today.

### What works

- **Boot & display**: ILI9341 SPI LCD with XPT2046 touch, LVGL UI, LED backlight control.
- **Wi-Fi**: station-mode auto-connect with reconnect handling. Credentials are seeded from Kconfig on first boot and persisted in NVS.
- **Wi-Fi provisioning AP**: if the device cannot connect, it starts a provisioning access point (`ESPSpotifyOS-Setup` by default) and serves a settings page to change the Wi-Fi network and restart. See [Wi-Fi Provisioning](#wi-fi-provisioning).
- **Spotify login**: embedded web server on port `8080` serves a login page; full OAuth Authorization Code flow with token persistence and automatic token refresh.
- **Library browsing**: tabbed library screen — **Spotify** (liked tracks, 24 per page, infinite scroll), **Server** (tracks on the media server), and **SD** (local cache). Long-press an item to act on it.
- **Import to the media server**: long-press a liked track — the server searches for matching audio (yt-dlp), downloads it, and stores it. Deduplication is automatic (re-imports return the existing track).
- **SD cache / offline**: imported and downloaded tracks are streamed to the SD card (FatFS over the shared SPI bus) so they are available offline. The SD tab lists cached files. *(SD hardware path is implemented but not yet validated on-device.)*
- **Media server (backend)**: track search/download via `yt-dlp`, SQLite metadata store, MP3 storage, paginated listing, query/URL import, and a streaming endpoint with HTTP Range support. See [`server/`](server/README.md).

### What's next

- **Audio playback**: no audio output path exists yet — no I2S/DAC/codec code. `track_cache` already streams track audio into the SD cache, ready to tee into a decoder/player.
- **Playback actions**: play/pause, next/previous, and volume are planned but not implemented (tap on a track currently only selects it).

---

## Hardware Requirements

- **MCU**: ESP32 / ESP32-S3 (or compatible ESP-IDF supported target)
- **Display**: ILI9341 SPI TFT LCD (with LED backlight control via LEDC)
- **Touch**: XPT2046 SPI Touch Controller
- **Optional**: SD Card Reader, External DAC/I2S Codec

---

## Project Structure

```
ESPSpotifyOS/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── components/
│   └── ui/                        # LVGL UI components and screens (PicoPixel-generated shells)
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
│   │   ├── lcd.c/.h                # LCD, backlight, touch, LVGL task
│   │   ├── library_list.c/.h       # generic paged list widget (selection/long-press)
│   │   ├── library_screen.c/.h     # library screen shell + tab bar
│   │   ├── library_source.h        # data-source interface (Spotify/Server/SD)
│   │   ├── library_sources.c       # source adapters
│   │   ├── setup_screen.c/.h       # setup screen status helper
│   │   └── ui_state.c/.h           # UI state sync loop
│   ├── net/
│   │   ├── http_client.c/.h        # HTTP client wrapper (GET/POST/stream)
│   │   ├── media_client.c/.h       # companion media server client
│   │   ├── spotify_auth.c/.h       # Spotify OAuth + token lifecycle
│   │   └── spotify_api.c/.h        # Spotify Web API (profile, saved tracks)
│   ├── server/
│   │   ├── webserver.c/.h          # embedded HTTP server lifecycle + route registration
│   │   └── routes.c/.h             # HTTP endpoints (/submit OAuth, /settings, /wifi-scan)
│   ├── services/
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

### 2. Configure the Project

Open the configuration menu:
```bash
idf.py menuconfig
```

Configure Wi-Fi and Spotify credentials:
- `CONFIG_ESP_WIFI_SSID` / `CONFIG_ESP_WIFI_PASSWORD`: your Wi-Fi network credentials
- `CONFIG_PLAYER_SPOTIFY_CLIENT_ID` / `CONFIG_PLAYER_SPOTIFY_CLIENT_SECRET`: your Spotify app credentials (from the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard))
- `CONFIG_PLAYER_MEDIA_SERVICE_URL` / `CONFIG_PLAYER_MEDIA_API_KEY`: the media server's address (e.g. `http://192.168.1.50:8081`) and the `SECRET` value from its `.env` file. Needed for the long-press import feature.
- `CONFIG_PLAYER_AP_SSID` / `CONFIG_PLAYER_AP_PASSWORD`: the provisioning access point (see below). The AP password must be at least 8 characters, or empty for an open AP.

### 3. Build and Flash

Build, flash, and open the serial monitor:
```bash
idf.py -p <PORT> flash monitor
```
*(To exit the monitor, press `Ctrl + ]`)*

---

## Wi-Fi Provisioning

On boot the device tries to join the configured network. If that fails, it
starts a provisioning access point instead and shows the AP name plus the setup
URL on the screen:

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
| `GET` | `/submit` | Login page — builds a Spotify OAuth URL and lets you paste the authorization code back |
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