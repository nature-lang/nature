package helper

import (
	"fmt"
	logger "log"
	"os"
	"path/filepath"
	"runtime"
)

var (
	Verbose = false
)

func Logf(format string, a ...any) {
	verbose := os.Getenv("PARKER_VERBOSE")
	if verbose == "" {
		return
	}

	logger.SetFlags(logger.Ltime)
	_, file, line, _ := runtime.Caller(1)
	fmt.Printf("log in %s:%d ", filepath.Base(file), line)
	fmt.Printf(format, a...)
	fmt.Println()
}
