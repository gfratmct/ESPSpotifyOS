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
- **Wi-Fi**: station-mode auto-connect with reconnect handling (credentials via Kconfig / stored state).
- **Spotify login**: embedded web server on port `8080` serves a login page; full OAuth Authorization Code flow with token persistence and automatic token refresh.
- **Library browsing**: scroll through your Spotify liked tracks on the touchscreen (24 tracks per page, infinite scroll).
- **Import to the media server**: long-press a liked track to import it — the server searches for matching audio (yt-dlp), downloads it, and stores it. Deduplication is automatic (re-imports return the existing track).
- **Media server (backend)**: track search/download via `yt-dlp`, SQLite metadata store, MP3 storage, paginated listing, query/URL import, and a streaming endpoint with HTTP Range support. See [`server/`](server/README.md).

### What's next

- **Audio playback**: no audio output path exists yet — no I2S/DAC/codec code, only dependency and Kconfig placeholders. The media client already has a `media_stream_url()` helper ready for the streaming player.
- **Playback actions**: play/pause, next/previous, and volume are planned but not implemented (tap on a track currently only selects it).
- **Runtime Wi-Fi provisioning**: a settings page exists (`main/web/settings.html`) but is not yet embedded/served; Wi-Fi is configured at build time.

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
│   └── ui/                        # LVGL UI components and screens
├── main/
│   ├── actions/
│   │   └── app_actions.c/.h        # UI button handlers and business logic
│   ├── connectivity/
│   │   └── wifi.c/.h               # Wi-Fi station management
│   ├── data/
│   │   ├── settings.c/.h           # NVS settings (unused / superseded)
│   │   └── state.c/.h              # Persistent app state (tokens, wifi, screen)
│   ├── display/
│   │   ├── home_ui.c/.h            # Home screen (liked-tracks browser)
│   │   ├── lcd.c/.h                # LCD, backlight, touch, LVGL task
│   │   └── ui_state.c/.h           # UI state sync loop
│   ├── server/
│   │   ├── http.c/.h               # Embedded HTTP server + OAuth routes
│   │   └── routes.c/.h             # Route stubs (todo)
│   ├── utils/
│   │   ├── http.c/.h               # HTTP client wrapper
│   │   ├── spotify.c/.h            # Spotify OAuth + API client
│   │   └── storage.c/.h            # NVS helper
│   ├── web/
│   │   ├── settings.html           # Wi-Fi settings form (draft, not served)
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

### 3. Build and Flash

Build, flash, and open the serial monitor:
```bash
idf.py -p <PORT> flash monitor
```
*(To exit the monitor, press `Ctrl + ]`)*

---

## Web Server Endpoints

Once connected to Wi-Fi, the ESP32 starts an HTTP web server on port `8080`:

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/submit` | Login page — builds a Spotify OAuth URL and lets you paste the authorization code back |
| `POST` | `/submit` | Accepts `spotify_code=<code>`, exchanges it for tokens, and stores them |

Example access from a browser or terminal:
```bash
curl -v http://<ESP32_IP>:8080/submit
```