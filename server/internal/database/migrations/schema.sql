CREATE TABLE IF NOT EXISTS tracks (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    yt_id      TEXT UNIQUE,
    title      TEXT NOT NULL,
    artist     TEXT,
    album      TEXT,
    duration   INTEGER NOT NULL DEFAULT 0,
    local_file TEXT,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);
