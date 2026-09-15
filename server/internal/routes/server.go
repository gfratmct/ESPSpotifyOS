package routes

import (
	"log"

	"github.com/gfratmct/ESPSpotifyOS/service/internal/config"
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
	return &Server{
		r:      gin.Default(),
		logger: logger,
		config: config,
	}
}

func (s *Server) Run() error {
	if err := s.r.Run(s.config.Host + ":" + s.config.Port); err != nil {
		return err
	}
	return nil
}
