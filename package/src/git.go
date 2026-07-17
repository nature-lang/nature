package src

import (
	"fmt"
	"github.com/go-git/go-git/v5"
	"github.com/go-git/go-git/v5/plumbing"
	"os"
	"strings"
)

// ~/.nature/package/sources/github.com/BurntSushi/toml v1.3.2
// -->
// ~/.nature/package/sources/github.com.BurntSushi.toml@v1.3.2
func dstDir(url, version string) string {
	url = strings.TrimLeft(url, "/")
	name := strings.Replace(url, "/", ".", -1)
	return fmt.Sprintf("%s/%s@%s", packageSourcesDir, name, version)
}

func gitPull(url, version, dst string) error {
	return gitPullURL(fmt.Sprintf("https://%s", url), version, dst)
}

func gitPullURL(url, version, dst string) error {
	referenceName := plumbing.ReferenceName(version)
	if !referenceName.IsBranch() && !referenceName.IsTag() {
		referenceName = plumbing.NewTagReferenceName(version)
	}

	// 清空 dst 目录
	err := os.RemoveAll(dst)
	if err != nil {
		return fmt.Errorf("cannot remove dst=%v, err=%v", dst, err)
	}

	_, err = git.PlainClone(dst, false, &git.CloneOptions{
		URL:           url,
		Progress:      os.Stdout,
		ReferenceName: referenceName,
	})
	if err != nil {
		return fmt.Errorf("git clone repo=%v, tag version=%v err=%v", url, version, err)
	}

	return nil
}
