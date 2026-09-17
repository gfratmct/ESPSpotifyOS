#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle of a background transfer (media-server import, SD download, ...).
typedef enum {
    TRANSFER_IDLE = 0,
    TRANSFER_RUNNING,
    TRANSFER_DONE_NEW,        // imported for the first time
    TRANSFER_DONE_DUPLICATE,  // already present on the server
    TRANSFER_DONE_FAILED,
} transfer_state_t;

/**
 * @brief Queue a media-server import of `query` ("artist - title").
 *
 * Starts the worker task/queue lazily on first use. Returns false when a
 * transfer is already running, the queue is full, or the worker cannot start.
 */
bool transfer_manager_import(const char *query);

/**
 * @brief Queue a download of media-server track `track_id` into the SD cache.
 *        Same return semantics as transfer_manager_import().
 */
bool transfer_manager_download(int track_id, const char *encoding, const char *display_name);

// Current transfer state. Safe to read from any task.
transfer_state_t transfer_manager_state(void);

// Error text for TRANSFER_DONE_FAILED (empty string otherwise).
const char *transfer_manager_error(void);

// Returns the state to TRANSFER_IDLE. Called by the UI once a terminal state
// has been consumed.
void transfer_manager_clear(void);

#ifdef __cplusplus
}
#endif
