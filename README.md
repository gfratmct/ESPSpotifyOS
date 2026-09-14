# ESPSpotifyOS

An ESP-IDF application for ESP32 running an interactive Spotify controller UI with LVGL, ILI9341 display, XPT2046 touch controller, Wi-Fi networking, and an embedded HTTP server/client for Spotify authentication and control.

---

## Features

- **LVGL UI**: Graphic user interface rendered on an SPI LCD (ILI9341) with touch control (XPT2046).
- **Wi-Fi Connectivity**: Station mode auto-connect with event handling and power-save management.
- **Embedded Web Server (`esp_http_server`)**: Exposes local HTTP endpoints to receive OAuth callbacks, configuration, and control triggers.
- **HTTP Client (`esp_http_client`)**: Built-in HTTP GET and POST client with support for headers, status codes, and response payloads.
- **Audio Codec & Storage Support**: Integrated ESP audio codec, SD card (SDMMC/SDSPI), and FATFS.

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
├── `CMakeLists.txt`
├── `partitions.csv`
├── `sdkconfig.defaults`
├── components/
│   └── ui/                     # LVGL UI components and screens
├── main/
│   ├── `app_actions.c` / .h      # UI button handlers and business logic
│   ├── `http.c` / .h             # HTTP server and client abstraction
│   ├── lcd.c / .h              # LCD and backlight driver initialization
│   ├── `main.c`                  # Application entry point
│   ├── settings.c / .h         # NVS settings storage
│   └── `wifi.c` / .h             # Wi-Fi station management
└── managed_components/         # ESP-IDF component manager dependencies
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

Configure Wi-Fi settings under **Example Connection Configuration** (or Project Configuration):
- `CONFIG_ESP_WIFI_SSID`: Your Wi-Fi network SSID
- `CONFIG_ESP_WIFI_PASSWORD`: Your Wi-Fi password
- `CONFIG_ESP_MAXIMUM_RETRY`: Connection retry attempts

### 3. Build and Flash

Build, flash, and open the serial monitor:
```bash
idf.py -p <PORT> flash monitor
```
*(To exit the monitor, press `Ctrl + ]`)*

---

## Web Server Endpoints

Once connected to Wi-Fi, the ESP32 starts an HTTP web server on port `8080` (or `80`):

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/` | Home / Status check |
| `GET` | `/submit` | Test / OAuth callback submission |

Example access from browser or terminal:
```bash
curl -v http://<ESP32_IP>:8080/submit
```

---

## License

This project is licensed under the Apache 2.0 License.