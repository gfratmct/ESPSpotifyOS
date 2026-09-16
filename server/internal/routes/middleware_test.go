package routes

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
)

func TestRequireAPIKey(t *testing.T) {
	gin.SetMode(gin.TestMode)

	tests := []struct {
		name       string
		secret     string
		header     string
		value      string
		wantStatus int
	}{
		{name: "no secret configured", secret: "", wantStatus: http.StatusOK},
		{name: "missing key", secret: "s3cret", wantStatus: http.StatusUnauthorized},
		{name: "wrong key", secret: "s3cret", header: apiKeyHeader, value: "nope", wantStatus: http.StatusUnauthorized},
		{name: "valid key", secret: "s3cret", header: apiKeyHeader, value: "s3cret", wantStatus: http.StatusOK},
		{name: "valid bearer", secret: "s3cret", header: "Authorization", value: "Bearer s3cret", wantStatus: http.StatusOK},
		{name: "wrong bearer", secret: "s3cret", header: "Authorization", value: "Bearer nope", wantStatus: http.StatusUnauthorized},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			r := gin.New()
			r.GET("/tracks/:id/stream", requireAPIKey(tt.secret), func(c *gin.Context) {
				c.Status(http.StatusOK)
			})

			req := httptest.NewRequest(http.MethodGet, "/tracks/1/stream", nil)
			if tt.header != "" {
				req.Header.Set(tt.header, tt.value)
			}

			rec := httptest.NewRecorder()
			r.ServeHTTP(rec, req)

			if rec.Code != tt.wantStatus {
				t.Fatalf("got status %d, want %d", rec.Code, tt.wantStatus)
			}
		})
	}
}
