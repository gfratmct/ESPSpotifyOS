package config

import (
	"os"

	"github.com/joho/godotenv"
)

// godotenv file + enviroment

type Config struct {
	Host        string
	Port        string
	Debug       bool
	Secret      string
	LogLevel    string
	DBPath      string
	StoragePath string
}

func NewConfig() *Config {
	// load .env file if needed
	err := godotenv.Load()
	if err != nil {
		// handle error if needed
	}

	return &Config{
		Host:        os.Getenv("HOST"),
		Port:        os.Getenv("PORT"),
		Debug:       os.Getenv("DEBUG") == "true",
		Secret:      os.Getenv("SECRET"),
		LogLevel:    os.Getenv("LOG_LEVEL"),
		DBPath:      getEnv("DB_PATH", "data/espspotify.db"),
		StoragePath: getEnv("STORAGE_PATH", "storage"),
	}
}

func getEnv(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}
