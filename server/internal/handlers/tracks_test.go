package handlers

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"

	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/contexts"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/migrations"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/models"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/services"
	"github.com/gin-gonic/gin"
)

func newTestRouter(t *testing.T) (*gin.Engine, *services.TrackService) {
	t.Helper()
	gin.SetMode(gin.TestMode)

	ctx, err := contexts.CreateSQLite3Context(":memory:")
	if err != nil {
		t.Fatalf("create context: %v", err)
	}
	t.Cleanup(func() { ctx.Close() })

	if err := migrations.Apply(ctx.DB()); err != nil {
		t.Fatalf("apply migrations: %v", err)
	}

	trackService := services.NewTrackService(ctx, services.YouTubeDLPService{})
	handler := NewTrackHandler(trackService)

	r := gin.New()
	r.GET("/tracks", handler.List)
	r.POST("/tracks", handler.Import)
	r.GET("/tracks/:id/stream", handler.Stream)

	return r, trackService
}

func doJSON(t *testing.T, r *gin.Engine, method, path, body string) *httptest.ResponseRecorder {
	t.Helper()
	var reader *bytes.Reader
	if body == "" {
		reader = bytes.NewReader(nil)
	} else {
		reader = bytes.NewReader([]byte(body))
	}
	req := httptest.NewRequest(method, path, reader)
	req.Header.Set("Content-Type", "application/json")
	rec := httptest.NewRecorder()
	r.ServeHTTP(rec, req)
	return rec
}

func TestListTracksHandler(t *testing.T) {
	r, svc := newTestRouter(t)

	// empty -> tracks: [], total: 0
	rec := doJSON(t, r, http.MethodGet, "/tracks", "")
	if rec.Code != http.StatusOK {
		t.Fatalf("empty list status = %d, want 200", rec.Code)
	}
	var emptyBody struct {
		Tracks []json.RawMessage `json:"tracks"`
		Total  int               `json:"total"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &emptyBody); err != nil {
		t.Fatalf("unmarshal empty response: %v", err)
	}
	if emptyBody.Tracks == nil {
		t.Fatal("expected tracks: [] for an empty library")
	}
	if emptyBody.Total != 0 {
		t.Fatalf("expected total 0, got %d", emptyBody.Total)
	}

	for i := 0; i < 3; i++ {
		if err := svc.CreateTrack(&models.TrackModel{
			Title:     "Track",
			Artist:    "Artist",
			LocalFile: "storage/track.mp3",
		}); err != nil {
			t.Fatalf("create track: %v", err)
		}
	}

	rec = doJSON(t, r, http.MethodGet, "/tracks", "")
	if rec.Code != http.StatusOK {
		t.Fatalf("list status = %d, want 200", rec.Code)
	}
	var body struct {
		Tracks []models.TrackModel `json:"tracks"`
		Total  int                 `json:"total"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatalf("unmarshal response: %v", err)
	}
	if len(body.Tracks) != 3 || body.Total != 3 {
		t.Fatalf("expected 3 tracks total 3, got %d total %d", len(body.Tracks), body.Total)
	}
	for _, track := range body.Tracks {
		if track.Encoding != "mp3" {
			t.Fatalf("expected encoding mp3, got %q", track.Encoding)
		}
	}

	// pagination: limit=2 offset=1
	rec = doJSON(t, r, http.MethodGet, "/tracks?limit=2&offset=1", "")
	if rec.Code != http.StatusOK {
		t.Fatalf("paginated status = %d, want 200", rec.Code)
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &body); err != nil {
		t.Fatalf("unmarshal paginated response: %v", err)
	}
	if len(body.Tracks) != 2 || body.Total != 3 {
		t.Fatalf("expected 2 of 3, got %d of %d", len(body.Tracks), body.Total)
	}
}

func TestImportTrackHandler(t *testing.T) {
	r, svc := newTestRouter(t)

	// invalid JSON body
	rec := doJSON(t, r, http.MethodPost, "/tracks", "{not json")
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("bad json status = %d, want 400", rec.Code)
	}

	// empty url field (also no query)
	rec = doJSON(t, r, http.MethodPost, "/tracks", `{"url":""}`)
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("empty url status = %d, want 400", rec.Code)
	}

	// url that cannot be parsed
	rec = doJSON(t, r, http.MethodPost, "/tracks", `{"url":"not a url"}`)
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("invalid url status = %d, want 400", rec.Code)
	}

	// neither url nor query
	rec = doJSON(t, r, http.MethodPost, "/tracks", `{}`)
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("empty body status = %d, want 400", rec.Code)
	}

	// already imported -> dedup returns existing without hitting the network
	if err := svc.CreateTrack(&models.TrackModel{
		YtID:  "dQw4w9WgXcQ",
		Title: "Never Gonna Give You Up",
	}); err != nil {
		t.Fatalf("seed track: %v", err)
	}

	rec = doJSON(t, r, http.MethodPost, "/tracks", `{"url":"https://youtu.be/dQw4w9WgXcQ"}`)
	if rec.Code != http.StatusOK {
		t.Fatalf("dedup import status = %d, want 200", rec.Code)
	}
	var track models.TrackModel
	if err := json.Unmarshal(rec.Body.Bytes(), &track); err != nil {
		t.Fatalf("unmarshal track: %v", err)
	}
	if track.ID == 0 || track.Title != "Never Gonna Give You Up" {
		t.Fatalf("unexpected track: %+v", track)
	}
}

func TestStreamTrackHandler(t *testing.T) {
	r, svc := newTestRouter(t)

	// invalid id
	rec := doJSON(t, r, http.MethodGet, "/tracks/abc/stream", "")
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("invalid id status = %d, want 400", rec.Code)
	}

	// unknown id
	rec = doJSON(t, r, http.MethodGet, "/tracks/9999/stream", "")
	if rec.Code != http.StatusNotFound {
		t.Fatalf("unknown id status = %d, want 404", rec.Code)
	}

	// track without a local file
	if err := svc.CreateTrack(&models.TrackModel{Title: "No file"}); err != nil {
		t.Fatalf("seed track: %v", err)
	}
	rec = doJSON(t, r, http.MethodGet, "/tracks/1/stream", "")
	if rec.Code != http.StatusNotFound {
		t.Fatalf("no-file track status = %d, want 404", rec.Code)
	}

	// valid track with a real file
	dir := t.TempDir()
	path := filepath.Join(dir, "track.mp3")
	content := []byte("ID3\x03\x00\x00\x00\x00\x00\x00fake mp3 bytes")
	if err := os.WriteFile(path, content, 0644); err != nil {
		t.Fatalf("write file: %v", err)
	}
	if err := svc.CreateTrack(&models.TrackModel{Title: "Stream me", LocalFile: path}); err != nil {
		t.Fatalf("seed stream track: %v", err)
	}

	rec = doJSON(t, r, http.MethodGet, "/tracks/2/stream", "")
	if rec.Code != http.StatusOK {
		t.Fatalf("stream status = %d, want 200", rec.Code)
	}
	if ct := rec.Header().Get("Content-Type"); ct != "audio/mpeg" {
		t.Fatalf("content-type = %q, want audio/mpeg", ct)
	}
	if enc := rec.Header().Get("X-Audio-Encoding"); enc != "mp3" {
		t.Fatalf("x-audio-encoding = %q, want mp3", enc)
	}
	if ar := rec.Header().Get("Accept-Ranges"); ar != "bytes" {
		t.Fatalf("accept-ranges = %q, want bytes", ar)
	}
	if !bytes.Equal(rec.Body.Bytes(), content) {
		t.Fatalf("streamed body mismatch: got %d bytes, want %d", rec.Body.Len(), len(content))
	}

	// Range request -> partial content
	req := httptest.NewRequest(http.MethodGet, "/tracks/2/stream", nil)
	req.Header.Set("Range", "bytes=0-9")
	rec2 := httptest.NewRecorder()
	r.ServeHTTP(rec2, req)
	if rec2.Code != http.StatusPartialContent {
		t.Fatalf("range status = %d, want 206", rec2.Code)
	}
	if !bytes.Equal(rec2.Body.Bytes(), content[:10]) {
		t.Fatalf("range body mismatch: %q", rec2.Body.Bytes())
	}
}