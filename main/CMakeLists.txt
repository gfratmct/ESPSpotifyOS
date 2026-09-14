idf_component_register(SRCS "http.c" "settings.c" "lcd.c" "app_actions.c" "wifi.c" "http.c" "main.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_http_client esp_http_server esp-tls esp_driver_spi esp_driver_ledc esp_timer esp_lcd esp_driver_dac esp_ringbuf esp_driver_sdspi sdmmc fatfs esp_audio_codec cjson bt lvgl mbedtls ui
                    INCLUDE_DIRS ".")

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,-u,act_spotify_connect")