package logger

import (
	"os"
	"time"

	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"

	"license-server/internal/config"
)

// Fields is a convenience type for log fields
type Fields map[string]interface{}

// Init initializes the logger based on configuration
func Init() {
	cfg := config.Get()

	// Set time format
	zerolog.TimeFieldFormat = time.RFC3339

	if cfg.LogFormat == "json" {
		// JSON output for production
		log.Logger = zerolog.New(os.Stdout).With().Timestamp().Logger()
	} else {
		// Pretty console output for development
		log.Logger = zerolog.New(zerolog.ConsoleWriter{
			Out:        os.Stdout,
			TimeFormat: "15:04:05",
			NoColor:    false,
		}).With().Timestamp().Logger()
	}

	// Set global log level
	zerolog.SetGlobalLevel(zerolog.InfoLevel)
}

// Debug logs a debug message
func Debug(event, msg string, fields Fields) {
	e := log.Debug().Str("event", event)
	for k, v := range fields {
		e = e.Interface(k, v)
	}
	e.Msg(msg)
}

// Info logs an info message
func Info(event, msg string, fields Fields) {
	e := log.Info().Str("event", event)
	for k, v := range fields {
		e = e.Interface(k, v)
	}
	e.Msg(msg)
}

// Warn logs a warning message
func Warn(event, msg string, fields Fields) {
	e := log.Warn().Str("event", event)
	for k, v := range fields {
		e = e.Interface(k, v)
	}
	e.Msg(msg)
}

// Error logs an error message
func Error(event, msg string, fields Fields) {
	e := log.Error().Str("event", event)
	for k, v := range fields {
		e = e.Interface(k, v)
	}
	e.Msg(msg)
}

// Fatal logs a fatal message and exits
func Fatal(event, msg string, fields Fields) {
	e := log.Fatal().Str("event", event)
	for k, v := range fields {
		e = e.Interface(k, v)
	}
	e.Msg(msg)
}
