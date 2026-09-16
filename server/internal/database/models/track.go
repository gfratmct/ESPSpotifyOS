package models

import "time"

type TrackModel struct {
	ID        int       `json:"id"`
	YtID      string    `json:"yt_id"`
	Title     string    `json:"title"`
	Artist    string    `json:"artist"`
	Album     string    `json:"album"`
	Duration  int       `json:"duration"` // Duration in seconds
	LocalFile string    `json:"local_file"`
	Encoding  string    `json:"encoding"` // codec token: mp3, wav, aac, flac, ogg, opus, ...
	CreatedAt time.Time `json:"created_at"`
	UpdatedAt time.Time `json:"updated_at"`
}
