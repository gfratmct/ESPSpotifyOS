# ESP Spotify Player — Media Service

> **Disclaimer**: This is an **unofficial, hobby project** built for **personal use only**. It is **not affiliated with, endorsed by, or connected to Spotify**. Do not use it for commercial purposes.

A small Go backend ("Media Service") that backs the ESP Spotify player: it imports audio tracks, stores them, and streams them back to clients.

## Mission

Part of the larger ESPSpotifyOS project. While the ESP32 provides the touchscreen UI, this service handles the heavy lifting:

1. **Search & download** — given a track (or a query), it finds matching audio via **yt-dlp** on YouTube Music and downloads it as an MP3.
2. **Store** — track metadata lives in a SQLite database; the MP3 files are kept on disk.
3. **Stream** — serves the stored media back to clients (like the ESP32) so they can play the imported library.

## Current State

A working first version — list, import, and stream are wired end to end.

### Implemented

- **yt-dlp integration** (`internal/services/ytdlp.go`): search YouTube Music (`Search`), download MP3 (`Download`, requires `ffmpeg`), and fetch track metadata (`GetTrackInfo`). Runs `yt-dlp` from `PATH`.
- **Track service** (`internal/services/tracks.go`): list + CRUD plus `ImportTrackBasedOnQuery` with dedup by YouTube ID, and flexible URL parsing (watch / youtu.be / shorts / embed / bare ID).
- **SQLite storage** (`internal/database`): single `tracks` table with WAL mode and foreign keys.
- **HTTP API** with a pre-shared API key (see below).

### HTTP Endpoints

All endpoints require the API key (see [Authentication](#authentication)).

| Method | Path                | Description                                              |
|--------|---------------------|----------------------------------------------------------|
| `GET`  | `/tracks`           | List all available tracks (`{"tracks": [...], "total": N}`). Optional `?limit=` & `?offset=` for pagination |
| `POST` | `/tracks`           | Import a track: `{"url": "..."}` (YouTube URL) or `{"query": "artist - title"}` (searches YT Music, imports first match) |
| `GET`  | `/tracks/:id/stream`| Stream the track MP3 (supports HTTP `Range` for seeking) |

Import returns `201` when the track was newly downloaded, `200` with the existing track when it was already in the library, `400` for unparsable input, and `404` when a query has no matches.

Every track in the list carries an `encoding` field (`mp3`, `wav`, `aac`, `flac`, `ogg`, `opus`, ...) so players know how to decode it up front.

Streaming serves the file with an explicit, content-sniffed `Content-Type` (`audio/mpeg`, `audio/wav`, ...), a short `X-Audio-Encoding` codec header, and full HTTP `Range` support (`206 Partial Content`, `Accept-Ranges: bytes`) so clients — like the ESP32 audio libraries — can detect the codec from the headers and seek while streaming.

## Requirements

- **Go 1.27+**
- **yt-dlp** on `PATH`
- **ffmpeg** on `PATH` (required for MP3 extraction)

## Configuration

Configuration is read from environment variables (optionally via a `.env` file, loaded with godotenv). Copy `.env.example` to `.env` and adjust:

```bash
cp .env.example .env
```

| Env var | Default | Description |
|---|---|---|
| `HOST` | — | Bind address (empty = all interfaces) |
| `PORT` | — | Listen port |
| `DB_PATH` | `data/espspotify.db` | SQLite database file |
| `STORAGE_PATH` | `storage` | Directory for downloaded MP3s |
| `DEBUG` | `false` | Reserved (parsed, not yet used) |
| `SECRET` | — | Pre-shared API key (see below) |
| `LOG_LEVEL` | — | Reserved (parsed, not yet used) |

### Authentication

Clients must send the `SECRET` value in the `X-API-Key` header (or as an `Authorization: Bearer` token). Generate a key with:

```bash
openssl rand -hex 32
```

Requests without a valid key receive `401`. If `SECRET` is empty the middleware is a no-op, so local development stays frictionless.

## Running

From the `server/` directory:

```bash
go run ./cmd/entry
```

Or with explicit env vars:

```bash
HOST=0.0.0.0 PORT=8081 go run ./cmd/entry
```

A separate manual smoke-test binary (`./cmd/test`) checks for `yt-dlp`, searches, downloads a track, and prototype-streams it:

```bash
go run ./cmd/test
```

## Tests

```bash
go test ./...
```

Unit tests cover the track service and URL extraction against an in-memory SQLite database.

## Project Layout

```
server/
├── cmd/
│   ├── entry/       # Main server entry point
│   └── test/        # Manual integration smoke binary
├── internal/
│   ├── config/      # Env/config loading
│   ├── database/    # SQLite context, migrations, models
│   ├── handlers/    # HTTP handlers
│   ├── routes/      # Route registration
│   ├── services/    # yt-dlp + track business logic
│   └── types/       # Shared types
├── storage/         # Downloaded MP3s (gitignored)
└── data/            # SQLite database (gitignored)
```