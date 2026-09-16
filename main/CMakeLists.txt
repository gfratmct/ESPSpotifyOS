set(COMPONENT_SRCS
  "main.c"
  "actions/app_actions.c"
  "connectivity/wifi.c"
  "data/settings.c"
  "data/state.c"
  "display/lcd.c"
  "display/home_ui.c"
  "display/ui_state.c"
  "server/http.c"
  "utils/http.c"
  "utils/media.c"
  "utils/spotify.c"
  "utils/storage.c"
  "utils/blob_store.c"
  "utils/stream_debug.c"
)

set(COMPONENT_EMBED_TXTFILES
  "web/submit.html"
)

set(CORE_DEPS
  esp_timer
  nvs_flash
  cjson
)

set(CONNECTIVITY_DEPS
  esp_wifi
  esp_netif
  lwip
  esp-tls
  mbedtls
  esp_http_client
  esp_http_server
  bt
)

set(DRIVER_DEPS
  esp_driver_spi
  esp_driver_ledc
  esp_driver_dac
  esp_driver_sdspi
  sdmmc
  fatfs
  esp_ringbuf
)

set(UI_AUDIO_DEPS
  esp_lcd
  lvgl
  ui
  esp_audio_codec
)

set(COMPONENT_PRIV_REQUIRES
  ${CORE_DEPS}
  ${CONNECTIVITY_DEPS}
  ${DRIVER_DEPS}
  ${UI_AUDIO_DEPS}
)

idf_component_register(
  SRCS ${COMPONENT_SRCS}
  INCLUDE_DIRS "."
  PRIV_REQUIRES ${COMPONENT_PRIV_REQUIRES}
  EMBED_TXTFILES ${COMPONENT_EMBED_TXTFILES}
)

set(PRESERVE_UI_ACTIONS
    "-Wl,-u,act_spotify_connect"
    # Add future UI action symbols here:
    # "-Wl,-u,act_play_pause"
    # "-Wl,-u,act_next_track"
)
# Preserve symbols from linker garbage collection
target_link_options(${COMPONENT_LIB} INTERFACE ${PRESERVE_UI_ACTIONS})
