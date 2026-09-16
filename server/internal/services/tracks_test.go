package services

import (
	"errors"
	"strconv"
	"testing"

	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/contexts"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/migrations"
	"github.com/gfratmct/ESPSpotifyOS/service/internal/database/models"
)

func newTestService(t *testing.T) *TrackService {
	t.Helper()

	ctx, err := contexts.CreateSQLite3Context(":memory:")
	if err != nil {
		t.Fatalf("create context: %v", err)
	}
	t.Cleanup(func() { ctx.Close() })

	if err := migrations.Apply(ctx.DB()); err != nil {
		t.Fatalf("apply migrations: %v", err)
	}

	return NewTrackService(ctx, YouTubeDLPService{})
}

func TestCreateAndGetTrack(t *testing.T) {
	svc := newTestService(t)

	track := &models.TrackModel{
		YtID:      "dQw4w9WgXcQ",
		Title:     "Never Gonna Give You Up",
		Artist:    "Rick Astley",
		Album:     "Whenever You Need Somebody",
		Duration:  213,
		LocalFile: "storage/dQw4w9WgXcQ.mp3",
	}

	if err := svc.CreateTrack(track); err != nil {
		t.Fatalf("create track: %v", err)
	}
	if track.ID == 0 {
		t.Fatal("expected an assigned id")
	}
	if track.CreatedAt.IsZero() || track.UpdatedAt.IsZero() {
		t.Fatal("expected timestamps to be set")
	}

	got, err := svc.GetTrackByID(track.ID)
	if err != nil {
		t.Fatalf("get track: %v", err)
	}
	if got.ID != track.ID || got.YtID != track.YtID || got.Title != track.Title ||
		got.Artist != track.Artist || got.Album != track.Album ||
		got.Duration != track.Duration || got.LocalFile != track.LocalFile {
		t.Fatalf("got %+v, want %+v", *got, *track)
	}
	if !got.CreatedAt.Equal(track.CreatedAt) || !got.UpdatedAt.Equal(track.UpdatedAt) {
		t.Fatalf("got timestamps %v/%v, want %v/%v", got.CreatedAt, got.UpdatedAt, track.CreatedAt, track.UpdatedAt)
	}

	if _, err := svc.GetTrackByID(9999); !errors.Is(err, ErrTrackNotFound) {
		t.Fatalf("got %v, want ErrTrackNotFound", err)
	}
}

func TestUpdateTrack(t *testing.T) {
	svc := newTestService(t)

	track := &models.TrackModel{Title: "Old title", Duration: 10}
	if err := svc.CreateTrack(track); err != nil {
		t.Fatalf("create track: %v", err)
	}

	track.Title = "New title"
	track.Artist = "New artist"
	track.Duration = 42
	if err := svc.UpdateTrack(track); err != nil {
		t.Fatalf("update track: %v", err)
	}

	got, err := svc.GetTrackByID(track.ID)
	if err != nil {
		t.Fatalf("get track: %v", err)
	}
	if got.Title != "New title" || got.Artist != "New artist" || got.Duration != 42 {
		t.Fatalf("got %+v, want updated fields", *got)
	}

	missing := &models.TrackModel{ID: 9999, Title: "nope"}
	if err := svc.UpdateTrack(missing); !errors.Is(err, ErrTrackNotFound) {
		t.Fatalf("got %v, want ErrTrackNotFound", err)
	}
}

func TestDeleteTrack(t *testing.T) {
	svc := newTestService(t)

	track := &models.TrackModel{Title: "Delete me"}
	if err := svc.CreateTrack(track); err != nil {
		t.Fatalf("create track: %v", err)
	}

	if err := svc.DeleteTrack(track.ID); err != nil {
		t.Fatalf("delete track: %v", err)
	}
	if _, err := svc.GetTrackByID(track.ID); !errors.Is(err, ErrTrackNotFound) {
		t.Fatalf("got %v, want ErrTrackNotFound", err)
	}
	if err := svc.DeleteTrack(track.ID); !errors.Is(err, ErrTrackNotFound) {
		t.Fatalf("got %v, want ErrTrackNotFound", err)
	}
}

func TestCreateTrackWithoutYtIDAllowsDuplicates(t *testing.T) {
	svc := newTestService(t)

	for i := 0; i < 2; i++ {
		if err := svc.CreateTrack(&models.TrackModel{Title: "local only"}); err != nil {
			t.Fatalf("create track %d: %v", i, err)
		}
	}
}

func TestListTracks(t *testing.T) {
	svc := newTestService(t)

	// empty database -> empty slice, zero total
	tracks, total, err := svc.ListTracks(0, 0)
	if err != nil {
		t.Fatalf("list empty tracks: %v", err)
	}
	if len(tracks) != 0 || total != 0 {
		t.Fatalf("expected no tracks, got %d tracks, total %d", len(tracks), total)
	}

	for i := 0; i < 3; i++ {
		track := &models.TrackModel{
			YtID:   "id" + strconv.Itoa(i),
			Title:  "Track " + strconv.Itoa(i),
			Artist: "Artist",
		}
		if err := svc.CreateTrack(track); err != nil {
			t.Fatalf("create track %d: %v", i, err)
		}
	}

	tracks, total, err = svc.ListTracks(0, 0)
	if err != nil {
		t.Fatalf("list tracks: %v", err)
	}
	if len(tracks) != 3 || total != 3 {
		t.Fatalf("expected 3 tracks total 3, got %d tracks total %d", len(tracks), total)
	}
	for i, track := range tracks {
		if track.ID != i+1 || track.Title != "Track "+strconv.Itoa(i) {
			t.Fatalf("track %d: got %+v", i, track)
		}
	}

	// pagination: limit 2 offset 1 -> tracks 2 and 3, total still 3
	tracks, total, err = svc.ListTracks(2, 1)
	if err != nil {
		t.Fatalf("list paginated tracks: %v", err)
	}
	if total != 3 || len(tracks) != 2 {
		t.Fatalf("expected 2 of 3, got %d of %d", len(tracks), total)
	}
	if tracks[0].ID != 2 || tracks[1].ID != 3 {
		t.Fatalf("unexpected page ids: %d, %d", tracks[0].ID, tracks[1].ID)
	}
}

func TestImportTrackBySearchQueryEmptyQuery(t *testing.T) {
	svc := newTestService(t)
	if _, _, err := svc.ImportTrackBySearchQuery("   "); !errors.Is(err, ErrInvalidURL) {
		t.Fatalf("got %v, want ErrInvalidURL for empty query", err)
	}
}

func TestExtractVideoID(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		want    string
		wantErr bool
	}{
		{name: "watch", input: "https://www.youtube.com/watch?v=dQw4w9WgXcQ", want: "dQw4w9WgXcQ"},
		{name: "music", input: "https://music.youtube.com/watch?v=dQw4w9WgXcQ&list=RDAMVM", want: "dQw4w9WgXcQ"},
		{name: "short link", input: "https://youtu.be/dQw4w9WgXcQ", want: "dQw4w9WgXcQ"},
		{name: "shorts", input: "https://www.youtube.com/shorts/dQw4w9WgXcQ", want: "dQw4w9WgXcQ"},
		{name: "embed", input: "https://www.youtube.com/embed/dQw4w9WgXcQ", want: "dQw4w9WgXcQ"},
		{name: "bare id", input: "dQw4w9WgXcQ", want: "dQw4w9WgXcQ"},
		{name: "empty", input: "  ", wantErr: true},
		{name: "not youtube", input: "https://example.com/whatever", wantErr: true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := extractVideoID(tt.input)
			if tt.wantErr {
				if err == nil {
					t.Fatalf("expected error, got %q", got)
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if got != tt.want {
				t.Fatalf("got %q, want %q", got, tt.want)
			}
		})
	}
}
