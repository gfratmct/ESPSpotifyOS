package database

import (
	"database/sql"
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

func CreateSQLite3Context(dataSourceName string) (*SQLite3Context, error) {
	db, err := sql.Open("sqlite3", dataSourceName)
	if err != nil {
		return nil, err
	}
	return NewSQLite3Context(db), nil
}
