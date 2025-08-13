package src

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
)

func throw(format string, a ...any) {
	_, file, line, _ := runtime.Caller(1)
	fmt.Printf("panic in %s:%d ", filepath.Base(file), line)
	fmt.Printf(format, a...)
	fmt.Println()
	os.Exit(1)
}
