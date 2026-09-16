package services

import (
	"database/sql"
	"errors"
	"fmt"
	"net/url"
	"strings"
	"time"

	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/contexts"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/models"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/types"
)

var (
	ErrTrackNotFound = errors.New("track not found")
	ErrInvalidURL    = errors.New("invalid url")
	ErrNoResults     = errors.New("no matching track found")
)

const trackColumns = "id, yt_id, title, artist, album, duration, local_file, created_at, updated_at"

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
	if track == nil {
		return fmt.Errorf("nil track")
	}

	now := time.Now().UTC()
	res, err := s.ctx.DB().Exec(
		`INSERT INTO tracks (yt_id, title, artist, album, duration, local_file, created_at, updated_at)
		 VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
		nullable(track.YtID),
		track.Title,
		nullable(track.Artist),
		nullable(track.Album),
		track.Duration,
		nullable(track.LocalFile),
		now,
		now,
	)
	if err != nil {
		return fmt.Errorf("insert track: %w", err)
	}

	id, err := res.LastInsertId()
	if err != nil {
		return fmt.Errorf("last insert id: %w", err)
	}

	track.ID = int(id)
	track.CreatedAt = now
	track.UpdatedAt = now

	return nil
}

func (s *TrackService) GetTrackByID(id int) (*models.TrackModel, error) {
	return scanTrack(s.ctx.DB().QueryRow(
		"SELECT "+trackColumns+" FROM tracks WHERE id = ?", id,
	))
}

// ListTracks returns imported tracks in import order. When limit > 0 the
// result is a page of at most `limit` tracks starting at `offset`. It always
// returns the total number of tracks in the library.
func (s *TrackService) ListTracks(limit, offset int) ([]models.TrackModel, int, error) {
	query := "SELECT " + trackColumns + " FROM tracks ORDER BY id"
	if limit > 0 {
		query += fmt.Sprintf(" LIMIT %d OFFSET %d", limit, offset)
	}

	rows, err := s.ctx.DB().Query(query)
	if err != nil {
		return nil, 0, fmt.Errorf("list tracks: %w", err)
	}
	defer rows.Close()

	var tracks []models.TrackModel
	for rows.Next() {
		track, err := scanTrackRows(rows)
		if err != nil {
			return nil, 0, err
		}
		tracks = append(tracks, *track)
	}
	if err := rows.Err(); err != nil {
		return nil, 0, fmt.Errorf("list tracks: %w", err)
	}

	var total int
	if err := s.ctx.DB().QueryRow("SELECT COUNT(*) FROM tracks").Scan(&total); err != nil {
		return nil, 0, fmt.Errorf("count tracks: %w", err)
	}

	return tracks, total, nil
}

func (s *TrackService) UpdateTrack(track *models.TrackModel) error {
	if track == nil {
		return fmt.Errorf("nil track")
	}

	now := time.Now().UTC()
	res, err := s.ctx.DB().Exec(
		`UPDATE tracks
		 SET yt_id = ?, title = ?, artist = ?, album = ?, duration = ?, local_file = ?, updated_at = ?
		 WHERE id = ?`,
		nullable(track.YtID),
		track.Title,
		nullable(track.Artist),
		nullable(track.Album),
		track.Duration,
		nullable(track.LocalFile),
		now,
		track.ID,
	)
	if err != nil {
		return fmt.Errorf("update track: %w", err)
	}

	if err := checkRowAffected(res); err != nil {
		return err
	}

	track.UpdatedAt = now
	return nil
}

func (s *TrackService) DeleteTrack(id int) error {
	res, err := s.ctx.DB().Exec("DELETE FROM tracks WHERE id = ?", id)
	if err != nil {
		return fmt.Errorf("delete track: %w", err)
	}
	return checkRowAffected(res)
}

// this function will be the main entrypoint for when I need to import a new track
// It returns whether the track was just created or was already present.
func (s *TrackService) ImportTrackBasedOnQuery(rawURL string) (*models.TrackModel, bool, error) {
	ytID, err := extractVideoID(rawURL)
	if err != nil {
		return nil, false, fmt.Errorf("%w: %v", ErrInvalidURL, err)
	}
	return s.importByVideoID(ytID)
}

// ImportTrackBySearchQuery searches for a track by name/artist and imports the
// best (first) match. This is what the ESP32 uses when importing from Spotify,
// where only the artist and title are known.
func (s *TrackService) ImportTrackBySearchQuery(query string) (*models.TrackModel, bool, error) {
	if strings.TrimSpace(query) == "" {
		return nil, false, fmt.Errorf("%w: empty query", ErrInvalidURL)
	}

	results, err := s.ytdlpService.Search(query, 1)
	if err != nil {
		return nil, false, fmt.Errorf("search track: %w", err)
	}
	if len(results) == 0 {
		return nil, false, ErrNoResults
	}
	return s.importByVideoID(results[0].ID)
}

// importByVideoID deduplicates, downloads and stores a track by its YouTube ID.
func (s *TrackService) importByVideoID(ytID string) (*models.TrackModel, bool, error) {
	// already imported, nothing to do
	if existing, err := s.getTrackByYtID(ytID); err == nil {
		return existing, false, nil
	} else if !errors.Is(err, ErrTrackNotFound) {
		return nil, false, err
	}

	info, err := s.ytdlpService.GetTrackInfo(ytID)
	if err != nil {
		return nil, false, fmt.Errorf("fetch track info: %w", err)
	}

	media, err := s.ytdlpService.Download(types.SearchResult{
		ID:    info.ID,
		Title: info.Title,
		URL:   info.URL,
	}, "")
	if err != nil {
		return nil, false, fmt.Errorf("download track: %w", err)
	}

	id := info.ID
	if id == "" {
		id = ytID
	}

	track := &models.TrackModel{
		YtID:      id,
		Title:     info.Title,
		Artist:    derefString(info.Artist),
		Album:     info.Album,
		Duration:  int(info.Duration / time.Second),
		LocalFile: media.FilePath,
	}

	if err := s.CreateTrack(track); err != nil {
		return nil, false, err
	}

	return track, true, nil
}

// getTrackByYtID is used to deduplicate imported tracks.
func (s *TrackService) getTrackByYtID(ytID string) (*models.TrackModel, error) {
	if ytID == "" {
		return nil, ErrTrackNotFound
	}
	return scanTrack(s.ctx.DB().QueryRow(
		"SELECT "+trackColumns+" FROM tracks WHERE yt_id = ?", ytID,
	))
}

func scanTrack(row *sql.Row) (*models.TrackModel, error) {
	return scanTrackScanner(row)
}

func scanTrackRows(rows *sql.Rows) (*models.TrackModel, error) {
	return scanTrackScanner(rows)
}

func scanTrackScanner(scanner interface{ Scan(...any) error }) (*models.TrackModel, error) {
	var (
		track     models.TrackModel
		ytID      sql.NullString
		artist    sql.NullString
		album     sql.NullString
		localFile sql.NullString
	)

	err := scanner.Scan(
		&track.ID,
		&ytID,
		&track.Title,
		&artist,
		&album,
		&track.Duration,
		&localFile,
		&track.CreatedAt,
		&track.UpdatedAt,
	)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, ErrTrackNotFound
		}
		return nil, fmt.Errorf("scan track: %w", err)
	}

	track.YtID = ytID.String
	track.Artist = artist.String
	track.Album = album.String
	track.LocalFile = localFile.String

	return &track, nil
}

func checkRowAffected(res sql.Result) error {
	affected, err := res.RowsAffected()
	if err != nil {
		return fmt.Errorf("rows affected: %w", err)
	}
	if affected == 0 {
		return ErrTrackNotFound
	}
	return nil
}

// nullable maps empty strings to NULL so unique/optional columns stay clean.
func nullable(value string) any {
	if value == "" {
		return nil
	}
	return value
}

func derefString(value *string) string {
	if value == nil {
		return ""
	}
	return *value
}

// extractVideoID accepts a YouTube URL (watch, youtu.be, embed, shorts) or a bare video ID.
func extractVideoID(rawURL string) (string, error) {
	rawURL = strings.TrimSpace(rawURL)
	if rawURL == "" {
		return "", fmt.Errorf("empty url")
	}

	if u, err := url.Parse(rawURL); err == nil && u.Host != "" {
		host := strings.ToLower(strings.TrimPrefix(u.Host, "www."))
		switch {
		case host == "youtu.be":
			if id := strings.Trim(u.Path, "/"); id != "" {
				return id, nil
			}
		case strings.HasSuffix(host, "youtube.com"):
			if id := u.Query().Get("v"); id != "" {
				return id, nil
			}
			parts := strings.Split(strings.Trim(u.Path, "/"), "/")
			if len(parts) == 2 && (parts[0] == "embed" || parts[0] == "shorts") {
				return parts[1], nil
			}
		}
	}

	// allow passing a bare video id
	if len(rawURL) == 11 && !strings.ContainsAny(rawURL, "/:.?&=") {
		return rawURL, nil
	}

	return "", fmt.Errorf("could not extract youtube id from %q", rawURL)
}
