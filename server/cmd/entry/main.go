package main

import (
	"log"
	"os"

	"github.com/gfratmct/ESPSpotifyOS/service/internal/config"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/contexts"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/migrations"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/handlers"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/routes"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/services"
)

func main() {
	logger := log.New(os.Stdout, "[media-service] ", log.LstdFlags|log.Lshortfile)

	cfg := config.NewConfig()

	if cfg.Secret == "" {
		logger.Println("warning: SECRET is not set, API key authentication is disabled")
	}

	// connect to the DB where the tracks are stored
	dbCtx, err := contexts.CreateSQLite3Context(cfg.DBPath)
	if err != nil {
		logger.Fatalf("failed to connect to database: %v", err)
	}
	defer dbCtx.Close()

	// apply the base schema (idempotent)
	if err := migrations.Apply(dbCtx.DB()); err != nil {
		logger.Fatalf("failed to apply migrations: %v", err)
	}

	// youtube-dl for downloading media content
	ytdlpService := services.NewYouTubeDLPService(cfg.StoragePath)

	trackService := services.NewTrackService(dbCtx, *ytdlpService)
	trackHandler := handlers.NewTrackHandler(trackService)

	server := routes.NewServer(cfg, logger)
	server.RegisterTrackHandler(trackHandler)

	logger.Printf("listening on %s:%s", cfg.Host, cfg.Port)
	if err := server.Run(); err != nil {
		logger.Fatalf("server stopped: %v", err)
	}
}

// expose an HTTP server for handling requests
// connect to a DB where to store the data
// youtube-dl for downloading media content
// handle session managment, only approved client can access this service
