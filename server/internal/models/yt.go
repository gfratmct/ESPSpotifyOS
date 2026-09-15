package models

import "time"

type MediaItem struct {
	ID       string
	Title    string
	Artist   *string
	URL      string
	FilePath string
}

type SearchResult struct {
	ID    string
	Title string
	URL   string
}

type TrackInfo struct {
	ID        string
	Title     string
	Artist    *string
	Album     string
	Duration  time.Duration
	Thumbnail string
	ViewCount int64
	URL       string
}
