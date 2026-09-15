package models

type TrackModel struct {
	ID        int
	Title     string
	Artist    string
	Album     string
	Duration  int // Duration in seconds
	LocalFile string
}
