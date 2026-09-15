package services

import "github.com/gfratmct/ESPSpotifyOS/service/internal/database"

type TrackService struct {
	ctx          *database.SQLite3Context
	ytdlpService YouTubeDLPService
}

func NewTrackService(ctx *database.SQLite3Context, ytdlpService YouTubeDLPService) *TrackService {
	return &TrackService{
		ctx:          ctx,
		ytdlpService: ytdlpService,
	}
}

// temp TrackModel , todo move under models
type TrackModel struct {
	ID        int
	Title     string
	Artist    string
	Album     string
	Duration  int // Duration in seconds
	LocalFile string
}

// BASIC CRUD

func (s *TrackService) CreateTrack(track *TrackModel) error {
	// Implement the logic to insert the track into the database using s.ctx
	return nil
}

func (s *TrackService) GetTrackByID(id int) (*TrackModel, error) {
	// Implement the logic to retrieve the track from the database using s.ctx
	return nil, nil
}

func (s *TrackService) UpdateTrack(track *TrackModel) error {
	// Implement the logic to update the track in the database using s.ctx
	return nil
}

func (s *TrackService) DeleteTrack(id int) error {
	// Implement the logic to delete the track from the database using s.ctx
	return nil
}

// this function will be the main entrypoint for when I need to import a new track
func (s *TrackService) ImportTrackBasedOnQuery(url string) (*TrackModel, error) {
	// Implement the logic to import the track from YouTube using s.ytdlpService
	return nil, nil
}
