package services

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/gfratmct/ESPSpotifyOS/service/internal/types"
)

type YouTubeDLPService struct {
	DefaultStoragePath string // here where I will store the media downloaded by yt-dlp
}

func NewYouTubeDLPService(defaultStoragePath string) *YouTubeDLPService {
	return &YouTubeDLPService{
		DefaultStoragePath: defaultStoragePath,
	}
}

// check if executable exists in the system path
// download helper function to download youtube-dl script
// get function
// search endpoint
// stream endpoint

// search method

func (s *YouTubeDLPService) Search(query string, limit int) ([]types.SearchResult, error) {
	// Implement the search functionality using youtube-dl
	// Return a list of video URLs or IDs matching the query
	if limit <= 0 {
		limit = 10
	} // default limit to 10 if not specified

	// #songs restricts results to the YT Music song catalog, not general videos
	searchURL := fmt.Sprintf("https://music.youtube.com/search?q=%s#songs", url.QueryEscape(query))

	args := []string{
		"--dump-json",
		"--flat-playlist",
		"--no-warnings",
		"--playlist-end", strconv.Itoa(limit),
		searchURL,
	}
	out, err := s.runCommand(args...)
	if err != nil {
		return nil, err
	}

	var items []types.SearchResult
	scanner := bufio.NewScanner(strings.NewReader(out))
	scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024) // yt-dlp lines can be long
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			continue
		}
		var raw struct {
			ID    string `json:"id"`
			Title string `json:"title"`
		}
		if err := json.Unmarshal([]byte(line), &raw); err != nil {
			continue // skip malformed line rather than failing whole search
		}
		items = append(items, types.SearchResult{
			ID:    raw.ID,
			Title: raw.Title,
			URL:   "https://music.youtube.com/watch?v=" + raw.ID,
		})
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return items, nil
}

// Download method to download a media item using yt-dlp
func (s *YouTubeDLPService) Download(item types.SearchResult, path string) (types.MediaItem, error) {
	// Implement the download functionality using yt-dlp
	// Return the downloaded media item or an error if the download fails
	dir := path
	if dir == "" {
		dir = s.DefaultStoragePath
	}
	if dir == "" {
		// throw error
		return types.MediaItem{}, fmt.Errorf("no storage path specified")
	}

	// check if ffmpeg is installed
	if !s.IsFfmpegInstalled() {
		return types.MediaItem{}, fmt.Errorf("ffmpeg is not installed")
	}

	// make a dir if not exists
	if _, err := os.Stat(dir); os.IsNotExist(err) {
		if err := os.MkdirAll(dir, 0755); err != nil {
			return types.MediaItem{}, fmt.Errorf("failed to create storage directory: %w", err)
		}
	}

	// for now only download mp3
	output := filepath.Join(dir, item.ID+".mp3")
	args := []string{
		"-x",
		"--audio-format", "mp3",
		"--no-warnings",
		"-o", output,
		item.URL,
	}

	if _, err := s.runCommand(args...); err != nil {
		return types.MediaItem{}, fmt.Errorf("failed to download media item: %w", err)
	}

	// check if file actually exists and size is greater than 0
	if fi, err := os.Stat(output); err != nil {
		return types.MediaItem{}, fmt.Errorf("failed to stat downloaded file: %w", err)
	} else if fi.Size() == 0 {
		return types.MediaItem{}, fmt.Errorf("downloaded file is empty")
	}

	return types.MediaItem{
		ID:       item.ID,
		Title:    item.Title,
		URL:      item.URL,
		FilePath: output,
	}, nil
}

// GetTrackInfo fetches full metadata for a single track by its video ID
func (s *YouTubeDLPService) GetTrackInfo(id string) (types.TrackInfo, error) {
	trackURL := "https://music.youtube.com/watch?v=" + id

	args := []string{
		"--dump-json",
		"--no-warnings",
		"--skip-download",
		trackURL,
	}

	out, err := s.runCommand(args...)
	if err != nil {
		return types.TrackInfo{}, err
	}

	var raw struct {
		ID         string   `json:"id"`
		Title      string   `json:"title"`
		Artists    []string `json:"artists"`
		Uploader   string   `json:"uploader"`
		Album      string   `json:"album"`
		Duration   float64  `json:"duration"`
		Thumbnail  string   `json:"thumbnail"`
		ViewCount  int64    `json:"view_count"`
		WebpageURL string   `json:"webpage_url"`
	}
	if err := json.Unmarshal([]byte(out), &raw); err != nil {
		return types.TrackInfo{}, fmt.Errorf("failed to parse track info: %w", err)
	}

	var artist *string
	switch {
	case len(raw.Artists) > 0:
		artist = &raw.Artists[0]
	case raw.Uploader != "":
		artist = &raw.Uploader
	}

	return types.TrackInfo{
		ID:        raw.ID,
		Title:     raw.Title,
		Artist:    artist,
		Album:     raw.Album,
		Duration:  time.Duration(raw.Duration * float64(time.Second)),
		Thumbnail: raw.Thumbnail,
		ViewCount: raw.ViewCount,
		URL:       raw.WebpageURL,
	}, nil
}

// private method to run yt-dlp commands and install youtube-dl if necessary
func (s *YouTubeDLPService) runCommand(args ...string) (string, error) {
	// Implement the logic to run yt-dlp commands
	// Install youtube-dl if necessary
	if !s.IsYtDlpInstalled() {
		return "", fmt.Errorf("yt-dlp is not installed")
	}

	fmt.Printf("Running yt-dlp with args: %v\n", args)
	fmt.Printf("Full command: yt-dlp %v\n", args)

	cmd := exec.Command("yt-dlp", args...)
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf("yt-dlp search failed: %w (%s)", err, stderr.String())
	}
	return stdout.String(), nil
}

// check if yt-dlp is installed and available in the system path
func (s *YouTubeDLPService) IsYtDlpInstalled() bool {
	// Implement the logic to check if yt-dlp is installed and available in the system path
	_, err := exec.LookPath("yt-dlp")
	if err != nil {
		fmt.Printf("Error: %s\n", err.Error())
		return false
	}
	return err == nil
}

// check if ffmpeg is installed
func (s *YouTubeDLPService) IsFfmpegInstalled() bool {
	_, err := exec.LookPath("ffmpeg")
	if err != nil {
		fmt.Printf("Error: %s\n", err.Error())
		return false
	}
	return err == nil
}
