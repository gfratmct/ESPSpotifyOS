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
	channel, err := h.trackService.StreamTrack(1)
	if err != nil {
		c.JSON(500, gin.H{"error": err.Error()})
		return
	}

	// set streaming
	c.Writer.Header().Set("Content-Type", "audio/mpeg")
	c.Writer.Header().Set("Transfer-Encoding", "chunked")
	c.Writer.Header().Set("Connection", "keep-alive")
	c.Writer.WriteHeader(200)

	for chunk := range channel {
		if chunk.Err != nil {
			c.JSON(500, gin.H{"error": chunk.Err.Error()})
			return
		}

		// return the data sse
		c.Writer.Write(chunk.Data)
		c.Writer.Flush()
	}
}
