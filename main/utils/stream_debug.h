#pragma once

/**
 * @brief Debug-stream a track and log the data as it arrives.
 *
 * Opens the media service's stream endpoint for `track_id`, reads the body
 * incrementally and logs chunk sizes / throughput, and forwards every chunk to
 * the (currently placeholder) blob store. Blocks until the stream ends or
 * fails, so it must run in a background task, never the LVGL task.
 *
 * @param track_id  media service track id
 * @param encoding  codec reported by the media service (mp3, wav, ...); may be NULL
 */
void stream_debug_track(int track_id, const char *encoding);
