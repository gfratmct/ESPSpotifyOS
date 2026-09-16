package contexts

import (
	"database/sql"
	"fmt"
	"os"
	"path/filepath"

	_ "github.com/mattn/go-sqlite3"
)

type SQLite3Context struct {
	db *sql.DB
	// Add other fields for database configuration if needed
}

func NewSQLite3Context(db *sql.DB) *SQLite3Context {
	return &SQLite3Context{
		db: db,
	}
}

func (ctx *SQLite3Context) DB() *sql.DB {
	return ctx.db
}

func (ctx *SQLite3Context) Close() error {
	return ctx.db.Close()
}

func CreateSQLite3Context(dataSourceName string) (*SQLite3Context, error) {
	// make sure the parent directory exists for file based databases
	if dataSourceName != ":memory:" {
		if dir := filepath.Dir(dataSourceName); dir != "" && dir != "." {
			if err := os.MkdirAll(dir, 0755); err != nil {
				return nil, fmt.Errorf("create database directory: %w", err)
			}
		}
	}

	db, err := sql.Open("sqlite3", dataSourceName)
	if err != nil {
		return nil, err
	}

	// a single connection avoids SQLITE_BUSY on concurrent writes and keeps
	// in-memory databases alive for the whole lifetime of the context
	db.SetMaxOpenConns(1)

	if err := db.Ping(); err != nil {
		db.Close()
		return nil, err
	}

	if _, err := db.Exec("PRAGMA busy_timeout = 5000;"); err != nil {
		db.Close()
		return nil, fmt.Errorf("set busy_timeout: %w", err)
	}
	if _, err := db.Exec("PRAGMA foreign_keys = ON;"); err != nil {
		db.Close()
		return nil, fmt.Errorf("enable foreign keys: %w", err)
	}

	// journal_mode returns the resulting mode as a row
	var journalMode string
	if err := db.QueryRow("PRAGMA journal_mode = WAL;").Scan(&journalMode); err != nil {
		db.Close()
		return nil, fmt.Errorf("set journal_mode: %w", err)
	}

	return NewSQLite3Context(db), nil
}
