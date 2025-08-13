package src

import (
	"github.com/stretchr/testify/assert"
	"testing"
)

func TestGtiPull(t *testing.T) {
	err := gitPull("github.com/BurntSushi/toml", "v1.3.2", "/tmp/foo")
	assert.NoError(t, err)
}
