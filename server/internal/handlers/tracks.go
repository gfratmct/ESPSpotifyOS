package handlers

import "github.com/gfratmct/ESPSpotifyOS/service/internal/services"

type TrackHandler struct {
	ytdlpService *services.YouTubeDLPService
}

func NewTrackHandler() *TrackHandler {
	return &TrackHandler{}
}
