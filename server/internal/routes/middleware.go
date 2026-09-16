package routes

import (
	"crypto/subtle"
	"strings"

	"github.com/gin-gonic/gin"
)

const apiKeyHeader = "X-API-Key"

// requireAPIKey guards routes with a pre-shared key. The ESP sends the key in
// the X-API-Key header (or as an Authorization: Bearer token). When no key is
// configured the middleware is a no-op so local development stays frictionless.
func requireAPIKey(secret string) gin.HandlerFunc {
	return func(c *gin.Context) {
		if secret == "" {
			c.Next()
			return
		}

		key := c.GetHeader(apiKeyHeader)
		if key == "" {
			if bearer, ok := strings.CutPrefix(c.GetHeader("Authorization"), "Bearer "); ok {
				key = bearer
			}
		}

		if subtle.ConstantTimeCompare([]byte(key), []byte(secret)) != 1 {
			c.AbortWithStatusJSON(401, gin.H{"error": "invalid or missing API key"})
			return
		}

		c.Next()
	}
}
