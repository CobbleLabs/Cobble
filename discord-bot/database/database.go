package database

import (
	"database/sql"
	"log"

	_ "github.com/mattn/go-sqlite3"
)

var DB *sql.DB

func Init(dbPath string) {
	var err error
	DB, err = sql.Open("sqlite3", dbPath)
	if err != nil {
		log.Fatal("Failed to open database:", err)
	}

	createTables()
	log.Println("Database initialized successfully")
}

func createTables() {
	schema := `
	CREATE TABLE IF NOT EXISTS tickets (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		channel_id TEXT UNIQUE NOT NULL,
		user_id TEXT NOT NULL,
		username TEXT NOT NULL,
		category TEXT NOT NULL,
		status TEXT DEFAULT 'open',
		claimed_by TEXT,
		created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
		closed_at TIMESTAMP,
		close_reason TEXT
	);

	CREATE TABLE IF NOT EXISTS messages (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		ticket_id INTEGER REFERENCES tickets(id),
		user_id TEXT NOT NULL,
		username TEXT NOT NULL,
		content TEXT NOT NULL,
		timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
	);

	CREATE INDEX IF NOT EXISTS idx_tickets_channel ON tickets(channel_id);
	CREATE INDEX IF NOT EXISTS idx_tickets_user ON tickets(user_id);
	CREATE INDEX IF NOT EXISTS idx_tickets_status ON tickets(status);
	`

	_, err := DB.Exec(schema)
	if err != nil {
		log.Fatal("Failed to create tables:", err)
	}
}

func Close() {
	if DB != nil {
		DB.Close()
	}
}
