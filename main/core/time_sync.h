#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Starts non-blocking SNTP so time(NULL) returns real epoch time after sync
// (needed for correct proactive access-token refresh across reboots).
void time_sync_start(void);

// Blocks until the first SNTP sync completes or `timeout_ms` elapses.
// Returns true on success.
bool time_sync_wait(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
