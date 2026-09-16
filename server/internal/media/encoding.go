package media

import (
	"path/filepath"
	"strings"

	"github.com/gabriel-vasile/mimetype"
)

// ContentType returns the audio MIME type for the given file, sniffing the
// file content first and falling back to the file extension.
func ContentType(path string) string {
	mt, err := mimetype.DetectFile(path)
	if err != nil {
		return ContentTypeFromName(path)
	}
	return normalize(mt.String())
}

func normalize(ct string) string {
	if ct == "application/ogg" {
		return "audio/ogg"
	}
	return ct
}

// ContentTypeFromName maps a file name/extension to an audio MIME type.
func ContentTypeFromName(name string) string {
	switch strings.ToLower(filepath.Ext(name)) {
	case ".wav":
		return "audio/wav"
	case ".ogg", ".oga":
		return "audio/ogg"
	case ".opus":
		return "audio/ogg"
	case ".flac":
		return "audio/flac"
	case ".aac":
		return "audio/aac"
	case ".m4a":
		return "audio/mp4"
	default:
		return "audio/mpeg"
	}
}

// Encoding returns a short codec token (mp3, wav, aac, flac, ogg, opus, ...)
// that audio clients can use to pick a decoder. It prefers content sniffing
// and falls back to the file extension.
func Encoding(path string) string {
	mt, err := mimetype.DetectFile(path)
	if err != nil {
		return EncodingFromName(path)
	}
	return codecFromMIME(mt.String())
}

// EncodingFromName derives a codec token from a file name only, so it is
// cheap enough to use for every track in a listing.
func EncodingFromName(name string) string {
	if strings.ToLower(filepath.Ext(name)) == ".opus" {
		return "opus"
	}
	return codecFromMIME(ContentTypeFromName(name))
}

func codecFromMIME(ct string) string {
	ct = strings.ToLower(ct)
	switch {
	case strings.Contains(ct, "mpeg"):
		return "mp3"
	case strings.Contains(ct, "wav"):
		return "wav"
	case strings.Contains(ct, "flac"):
		return "flac"
	case strings.Contains(ct, "aac"):
		return "aac"
	case strings.Contains(ct, "ogg"):
		if strings.Contains(ct, "opus") {
			return "opus"
		}
		return "ogg"
	case strings.Contains(ct, "mp4"):
		return "aac"
	default:
		if i := strings.IndexByte(ct, '/'); i >= 0 {
			return strings.TrimPrefix(ct[i+1:], "x-")
		}
		return ct
	}
}