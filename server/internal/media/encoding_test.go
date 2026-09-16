package media

import (
	"os"
	"path/filepath"
	"testing"
)

func TestContentTypeAndEncodingFromFile(t *testing.T) {
	dir := t.TempDir()

	tests := []struct {
		name        string
		content     []byte
		wantType    string
		wantEnc     string
	}{
		{
			name:     "id3 mp3",
			content:  []byte("ID3\x04\x00\x00\x00\x00\x00\x00fake"),
			wantType: "audio/mpeg",
			wantEnc:  "mp3",
		},
		{
			name:     "wav riff",
			content:  []byte("RIFF\x00\x00\x00\x00WAVEfmt "),
			wantType: "audio/wav",
			wantEnc:  "wav",
		},
		{
			name:     "ogg",
			content:  []byte("OggS\x00\x02"),
			wantType: "audio/ogg",
			wantEnc:  "ogg",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			path := filepath.Join(dir, "track.bin")
			if err := os.WriteFile(path, tt.content, 0644); err != nil {
				t.Fatalf("write file: %v", err)
			}

			if got := ContentType(path); got != tt.wantType {
				t.Fatalf("ContentType = %q, want %q", got, tt.wantType)
			}
			if got := Encoding(path); got != tt.wantEnc {
				t.Fatalf("Encoding = %q, want %q", got, tt.wantEnc)
			}
		})
	}
}

func TestEncodingFromName(t *testing.T) {
	tests := []struct {
		name string
		want string
	}{
		{"song.mp3", "mp3"},
		{"song.wav", "wav"},
		{"song.flac", "flac"},
		{"song.aac", "aac"},
		{"song.m4a", "aac"},
		{"song.ogg", "ogg"},
		{"song.opus", "opus"},
		{"unknown", "mp3"},
	}
	for _, tt := range tests {
		if got := EncodingFromName(tt.name); got != tt.want {
			t.Fatalf("EncodingFromName(%q) = %q, want %q", tt.name, got, tt.want)
		}
	}
}