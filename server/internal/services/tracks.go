package services

import (
	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/contexts"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/models"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/types"
)

type TrackService struct {
	ctx          *contexts.SQLite3Context
	ytdlpService YouTubeDLPService
}

func NewTrackService(ctx *contexts.SQLite3Context, ytdlpService YouTubeDLPService) *TrackService {
	return &TrackService{
		ctx:          ctx,
		ytdlpService: ytdlpService,
	}
}

// BASIC CRUD

func (s *TrackService) CreateTrack(track *models.TrackModel) error {
	// Implement the logic to insert the track into the database using s.ctx
	return nil
}

func (s *TrackService) GetTrackByID(id int) (*models.TrackModel, error) {
	// Implement the logic to retrieve the track from the database using s.ctx
	return nil, nil
}

func (s *TrackService) UpdateTrack(track *models.TrackModel) error {
	// Implement the logic to update the track in the database using s.ctx
	return nil
}

func (s *TrackService) DeleteTrack(id int) error {
	// Implement the logic to delete the track from the database using s.ctx
	return nil
}

// this function will be the main entrypoint for when I need to import a new track
func (s *TrackService) ImportTrackBasedOnQuery(url string) (*models.TrackModel, error) {
	// Implement the logic to import the track from YouTube using s.ytdlpService
	return nil, nil
}

// stream
func (s *TrackService) StreamTrack(id int) (<-chan types.StreamChunk, error) {
	// Implement the logic to stream the track from the local file or YouTube using s.ytdlpService
	return nil, nil
}
