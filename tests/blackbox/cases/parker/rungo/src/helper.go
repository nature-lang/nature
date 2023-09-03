package src

import "os"

func dirExists(dir string) bool {
	_, err := os.Stat(dir)
	return err == nil
}

func fileExists(file string) bool {
	_, err := os.Stat(file)
	return err == nil
}
