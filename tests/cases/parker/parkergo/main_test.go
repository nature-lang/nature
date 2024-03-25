package main

import (
	"gotest.tools/v3/assert"
	"os"
	"os/exec"
	"path"
	"testing"
)

func TestParker(t *testing.T) {
	t.Log("test parker start")

	workdir, err := os.Getwd()
	assert.NilError(t, err)
	t.Log("old workdir:", workdir)

	mockdir := path.Join(workdir, "tests/mockdir")
	runnerdir := path.Join(workdir, "tests/runnerdir")

	// runner/main.go patho
	runnerBuildFile := path.Join(workdir, "runner/main.go")
	cmd := exec.Command("/usr/local/go/bin/go", "build", "-o", "runner", runnerBuildFile)
	cmd.Dir = runnerdir
	err = cmd.Run()
	assert.NilError(t, err)

	cmd = exec.Command("/usr/local/go/bin/go", "build", "node.go")
	cmd.Dir = mockdir
	err = cmd.Run()
	assert.NilError(t, err)

	err = os.Chdir(mockdir)
	assert.NilError(t, err)
	workdir, err = os.Getwd()
	assert.NilError(t, err)
	t.Log("new workdir:", workdir)

	err = os.Setenv("RUNNER_PATH", path.Join(runnerdir, "runner"))
	assert.NilError(t, err)

	os.Args = []string{"parker", "node"}
	main()

	// exec noded get outpu
}
