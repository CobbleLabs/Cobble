package helpers

import (
	"regexp"
)

// UUID regex pattern
var uuidRegex = regexp.MustCompile(`^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$`)

// IsValidUUID checks if a string is a valid UUID format
func IsValidUUID(s string) bool {
	return uuidRegex.MatchString(s)
}
