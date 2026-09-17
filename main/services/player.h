#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Where a queued track comes from.
typedef enum {
    PLAYER_TRACK_SERVER, // stream from the media server by id
    PLAYER_TRACK_FILE,   // play a local file (SD cache)
} player_track_source_t;

typedef struct {
    player_track_source_t source;
    char title[128];
    char artist[128];
    int duration_s;      // -1 when unknown
    int media_id;        // PLAYER_TRACK_SERVER
    char encoding[16];   // mp3, wav, ...
    char path[288];      // PLAYER_TRACK_FILE
} player_track_t;

typedef enum {
    PLAYER_IDLE = 0,
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_ERROR,
} player_state_t;

typedef struct {
    player_state_t state;
    char title[128];
    char artist[128];
    int duration_s;
    int position_s;
    uint8_t volume;
    size_t index; // current track in the queue
    size_t count; // queue length
} player_status_t;

// Creates the player task and loads the persisted volume.
void player_init(void);

// Replaces the queue and starts playing at `start_index`. Takes a copy.
esp_err_t player_play_queue(const player_track_t *tracks, size_t count, size_t start_index);

void player_toggle_pause(void);
void player_next(void);
void player_prev(void);
void player_stop(void);

void player_set_volume(uint8_t percent);
uint8_t player_get_volume(void);

// Thread-safe snapshot for the UI.
void player_get_status(player_status_t *out);

// Last playback error (empty when none).
const char *player_error(void);

#ifdef __cplusplus
}
#endif
