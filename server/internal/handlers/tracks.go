package handlers

import (
	"github.com/gfratmct/ESPSpotifyOS/service/internal/services"
	"github.com/gin-gonic/gin"
)

type TrackHandler struct {
	trackService *services.TrackService
}

func NewTrackHandler(trackService *services.TrackService) *TrackHandler {
	return &TrackHandler{
		trackService: trackService,
	}
}

func (h *TrackHandler) Stream(c *gin.Context) {
	// Implement the logic to stream the track
}
