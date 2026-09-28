set(COMPONENT_SRCS
  "main.c"
  "connectivity/wifi.c"
  "core/time_sync.c"
  "data/auth_state.c"
  "data/device_state.c"
  "data/state.c"
  "display/display.c"
  "net/http_client.c"
  "net/media_client.c"
  "net/spotify_auth.c"
  "net/spotify_api.c"
  "server/webserver.c"
  "server/routes.c"
  "services/transfer_manager.c"
  "services/track_cache.c"
  "services/audio_output.c"
  "services/player.c"
  "storage/sd_storage.c"
  "utils/storage.c"
)

set(COMPONENT_EMBED_TXTFILES
  "web/submit.html"
  "web/settings.html"
)

set(CORE_DEPS
  esp_timer
  nvs_flash
  cjson
  esp_lcd
  lvgl
)

set(CONNECTIVITY_DEPS
  esp_wifi
  esp_netif
  lwip
  esp-tls
  mbedtls
  esp_http_client
  esp_http_server
)

set(DRIVER_DEPS
  esp_driver_spi
  esp_driver_i2s
  esp_driver_i2c
  esp_driver_sdspi
  esp_driver_gpio
  esp_driver_dac
  sdmmc
  fatfs
)

set(AUDIO_DEPS
  esp_audio_codec
)

set(COMPONENT_PRIV_REQUIRES
  ${CORE_DEPS}
  ${CONNECTIVITY_DEPS}
  ${DRIVER_DEPS}
  ${AUDIO_DEPS}
)

idf_component_register(
  SRCS ${COMPONENT_SRCS}
  INCLUDE_DIRS "."
  PRIV_REQUIRES ${COMPONENT_PRIV_REQUIRES}
  EMBED_TXTFILES ${COMPONENT_EMBED_TXTFILES}
)
