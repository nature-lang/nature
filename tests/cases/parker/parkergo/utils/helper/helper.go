package helper

import (
	"math/rand"
	"os"
)

const charset = "abcdefghijklmnopqrstuvwxyz0123456789"

func PathExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// Helper function to generate a random string with a given charset and length
func RandomLetter(length int) string {
	result := make([]byte, length)
	for i := range result {
		result[i] = charset[rand.Intn(len(charset))]
	}
	return string(result)
}
