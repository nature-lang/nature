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
	url = fmt.Sprintf("https://%s", url)

	// 清空 dst 目录
	err := os.RemoveAll(dst)
	if err != nil {
		return fmt.Errorf("cannot remove dst=%v, err=%v", dst, err)
	}

	_, err = git.PlainClone(dst, false, &git.CloneOptions{
		URL:           url,
		Progress:      os.Stdout,
		ReferenceName: plumbing.ReferenceName(version),
	})
	if err != nil {
		return fmt.Errorf("git clone repo=%v, tag version=%v err=%v", url, version, err)
	}

	return nil
}
