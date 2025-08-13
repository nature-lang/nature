package src

import (
	"fmt"
	"github.com/BurntSushi/toml"
)

const (
	PackageFile = "package.toml"
)

func Parser(file string) (Package, error) {
	var p Package
	if _, err := toml.DecodeFile(file, &p); err != nil {
		return p, fmt.Errorf("package.toml Parser failed: %v", err)
	}

	return p, nil
}
