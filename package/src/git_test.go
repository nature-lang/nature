package src

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/go-git/go-git/v5"
	"github.com/go-git/go-git/v5/plumbing/object"
)

func TestGitPull(t *testing.T) {
	source := t.TempDir()
	repo, err := git.PlainInit(source, false)
	if err != nil {
		t.Fatal(err)
	}

	worktree, err := repo.Worktree()
	if err != nil {
		t.Fatal(err)
	}

	const fixtureName = "fixture.txt"
	if err = os.WriteFile(filepath.Join(source, fixtureName), []byte("npkg fixture\n"), 0644); err != nil {
		t.Fatal(err)
	}
	if _, err = worktree.Add(fixtureName); err != nil {
		t.Fatal(err)
	}
	hash, err := worktree.Commit("create fixture", &git.CommitOptions{
		Author: &object.Signature{Name: "npkg test", Email: "npkg@example.invalid", When: time.Unix(1, 0)},
	})
	if err != nil {
		t.Fatal(err)
	}
	if _, err = repo.CreateTag("v1.0.0", hash, nil); err != nil {
		t.Fatal(err)
	}

	destination := filepath.Join(t.TempDir(), "clone")
	if err = gitPullURL(source, "v1.0.0", destination); err != nil {
		t.Fatal(err)
	}
	content, err := os.ReadFile(filepath.Join(destination, fixtureName))
	if err != nil {
		t.Fatal(err)
	}
	if string(content) != "npkg fixture\n" {
		t.Fatalf("unexpected fixture content: %q", content)
	}
}
