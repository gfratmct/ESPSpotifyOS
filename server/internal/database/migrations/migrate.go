package migrations

import (
	"database/sql"
	_ "embed"
	"fmt"
)

//go:embed schema.sql
var schema string

// Apply runs the base schema against the database. Every statement in
// schema.sql is idempotent, so it can be safely executed on each startup.
func Apply(db *sql.DB) error {
	if _, err := db.Exec(schema); err != nil {
		return fmt.Errorf("apply schema: %w", err)
	}
	return nil
}
