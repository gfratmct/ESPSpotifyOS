package routes

import (
	"log"

	"github.com/gfratmct/ESPSpotifyOS/service/internal/config"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/handlers"
	"github.com/gin-gonic/gin"
)

type Server struct {
	r      *gin.Engine
	logger *log.Logger
	config *config.Config
}

func NewServer(
	config *config.Config,
	logger *log.Logger,
) *Server {
	router := gin.Default()
	router.Use(allowCORS())

	return &Server{
		r:      router,
		logger: logger,
		config: config,
	}
}

func (s *Server) RegisterTrackHandler(handler *handlers.TrackHandler) {
	var tracks *gin.RouterGroup

	// if debug is enabled, allow all requests without API key
	if s.config.Debug {
		tracks = s.r.Group("/tracks")
	} else {
		tracks = s.r.Group("/tracks", requireAPIKey(s.config.Secret))
	}

	tracks.GET("", handler.List)
	tracks.POST("", handler.Import)
	tracks.GET("/:id/stream", handler.Stream)
}

func (s *Server) Run() error {
	if err := s.r.Run(s.config.Host + ":" + s.config.Port); err != nil {
		return err
	}
	return nil
}
