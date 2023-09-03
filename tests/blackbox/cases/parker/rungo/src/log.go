package src

import (
	"fmt"
	logger "log"
	"path/filepath"
	"runtime"
)

var (
	Verbose = false
)

func log(format string, a ...any) {
	if !Verbose {
		return
	}

	logger.SetFlags(logger.Ltime)
	_, file, line, _ := runtime.Caller(1)
	fmt.Printf("log in %s:%d ", filepath.Base(file), line)
	fmt.Printf(format, a...)
	fmt.Println()
}
