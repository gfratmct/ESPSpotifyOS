package handlers

import (
	"errors"
	"os"
	"strconv"

	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/models"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/media"
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

type importTrackRequest struct {
	URL   string `json:"url"`
	Query string `json:"query"`
}

// List returns the available tracks, optionally paginated via ?limit=&offset=.
func (h *TrackHandler) List(c *gin.Context) {
	limit, offset := 0, 0
	if v := c.Query("limit"); v != "" {
		limit, _ = strconv.Atoi(v)
	}
	if v := c.Query("offset"); v != "" {
		offset, _ = strconv.Atoi(v)
	}

	tracks, total, err := h.trackService.ListTracks(limit, offset)
	if err != nil {
		c.JSON(500, gin.H{"error": err.Error()})
		return
	}

	if tracks == nil {
		tracks = []models.TrackModel{}
	}

	for i := range tracks {
		tracks[i].Encoding = media.EncodingFromName(tracks[i].LocalFile)
	}

	c.JSON(200, gin.H{"tracks": tracks, "total": total})
}

// Import stores a track, either from a YouTube URL ("url") or by searching
// for it ("query").
func (h *TrackHandler) Import(c *gin.Context) {
	var req importTrackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(400, gin.H{"error": "invalid request body, expected {\"url\": \"...\"} or {\"query\": \"...\"}"})
		return
	}

	var (
		track   *models.TrackModel
		created bool
		err     error
	)
	switch {
	case req.URL != "":
		track, created, err = h.trackService.ImportTrackBasedOnQuery(req.URL)
	case req.Query != "":
		track, created, err = h.trackService.ImportTrackBySearchQuery(req.Query)
	default:
		c.JSON(400, gin.H{"error": "expected \"url\" or \"query\""})
		return
	}

	if err != nil {
		switch {
		case errors.Is(err, services.ErrInvalidURL):
			c.JSON(400, gin.H{"error": err.Error()})
		case errors.Is(err, services.ErrNoResults):
			c.JSON(404, gin.H{"error": err.Error()})
		default:
			c.JSON(500, gin.H{"error": err.Error()})
		}
		return
	}

	status := 200
	if created {
		status = 201
	}
	c.JSON(status, track)
}

// Stream serves the stored track with HTTP Range support.
func (h *TrackHandler) Stream(c *gin.Context) {
	id, err := strconv.Atoi(c.Param("id"))
	if err != nil {
		c.JSON(400, gin.H{"error": "invalid track id"})
		return
	}

	track, err := h.trackService.GetTrackByID(id)
	if err != nil {
		if errors.Is(err, services.ErrTrackNotFound) {
			c.JSON(404, gin.H{"error": "track not found"})
			return
		}
		c.JSON(500, gin.H{"error": err.Error()})
		return
	}

	if track.LocalFile == "" {
		c.JSON(404, gin.H{"error": "track has no local file"})
		return
	}

	if _, err := os.Stat(track.LocalFile); err != nil {
		c.JSON(404, gin.H{"error": "track file not found"})
		return
	}

	// tell audio clients how to decode the stream: an explicit, content-sniffed
	// Content-Type plus a short codec token, and seekable byte ranges.
	c.Writer.Header().Set("Content-Type", media.ContentType(track.LocalFile))
	c.Writer.Header().Set("X-Audio-Encoding", media.Encoding(track.LocalFile))
	c.Writer.Header().Set("Accept-Ranges", "bytes")

	c.File(track.LocalFile)
}