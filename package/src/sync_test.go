package src

import (
	"github.com/stretchr/testify/assert"
	"testing"
)

func TestHomeDir(t *testing.T) {
	initPackageDir()
	assert.NotEmpty(t, packageSourcesDir)
}
