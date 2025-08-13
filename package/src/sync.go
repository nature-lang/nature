package src

import (
	"fmt"
	cp "github.com/otiai10/copy"
	"os"
	"os/user"
	"strings"
)

var (
	handled           = make(map[string]bool)
	packageSourcesDir = ""
)

func Sync() {
	// 初始化 home dir
	initPackageDir()

	// 执行命令所在目录应当存在 package.toml 文件
	workdir, err := os.Getwd()
	if err != nil {
		throw("cannot get workdir, err=%v", err)
	}
	configFile := fmt.Sprintf("%s/%s", workdir, PackageFile)
	SyncByConfig(configFile)
}

// ~/.nature/package/sources
// ~/.nature/package/caches
func initPackageDir() {
	u, err := user.Current()
	if err != nil {
		throw("cannot get current user, err=%v", err)
	}
	homeDir := u.HomeDir
	if homeDir == "" {
		throw("cannot get home dir")
	}

	packageSourcesDir = fmt.Sprintf("%s/.nature/package/sources", homeDir)
	err = os.MkdirAll(packageSourcesDir, 0755)
	if err != nil {
		throw("cannot create dir=%v, err=%v", packageSourcesDir, err)
	}

	packageCachesDir := fmt.Sprintf("%s/.nature/package/caches", homeDir)
	err = os.MkdirAll(packageCachesDir, 0755)
	if err != nil {
		throw("cannot create dir=%v, err=%v", packageCachesDir, err)
	}
}

func SyncByConfig(configFile string) {
	// 已经处理过的 package 避免重复处理
	if _, ok := handled[configFile]; ok {
		return
	}

	// - 判断 package.toml 文件是否存在
	p, err := Parser(configFile)
	if err != nil {
		throw("file=%s parser err=%v", configFile, err)
	}

	// 循环便利下载依赖
	for key, dep := range p.Dependencies {
		if dep.Version == "" {
			throw("version cannot be empty")
		}

		depPath := ""
		if dep.Type == DependencyTypeGit {
			depPath = SyncGit(dep.Url, dep.Version)
		} else if dep.Type == DependencyTypeLocal {
			depPath = SyncLocal(key, dep.Path, dep.Version)
		} else {
			throw("unknown dependency type=%v", dep.Type)
		}

		// 配置文件, 递归
		configFile := fmt.Sprintf("%s/%s", depPath, PackageFile)
		SyncByConfig(configFile)
	}

	handled[configFile] = true
}

func SyncLocal(packageKey, path, version string) string {
	if path == "" || version == "" {
		throw("path or version cannot be empty")
	}

	// 检查目录是否存在
	if !dirExists(path) {
		throw("path=%v not exists", path)
	}

	dst := dstDir(packageKey, version)
	if dst == "" || dst == "/" || dst == "~" {
		throw("dst=%v is invalid", dst)
		return ""
	}
	if !strings.HasPrefix(dst, packageSourcesDir) {
		throw("dst=%v is invalid", dst)
		return ""
	}

	if dirExists(dst) {
		err := os.RemoveAll(dst)
		if err != nil {
			throw("cannot remove dir=%v, err=%v", dst, err)
		}
	}

	// 将 path 目录 copy 到 dst
	opt := cp.Options{
		Skip: func(info os.FileInfo, src, dest string) (bool, error) {
			return strings.HasSuffix(src, ".git"), nil
		},
	}
	err := cp.Copy(path, dst, opt)
	if err != nil {
		throw("cannot copy path=%v to dst=%v, err=%v", path, dst, err)
	}

	log("sync local success dst=%v", dst)
	return dst
}

func SyncGit(url, version string) string {
	if url == "" || version == "" {
		throw("url or version cannot be empty")
	}

	// - url 合法检测(不能携带 http 或者 https 前缀)
	if strings.HasPrefix(url, "http://") || strings.HasPrefix(url, "https://") {
		throw("url=%v cannot start with http or https", url)
	}

	dst := dstDir(url, version)
	packageFile := fmt.Sprintf("%s/%s", dst, PackageFile)

	if !dirExists(dst) || !fileExists(packageFile) {
		err := gitPull(url, version, dst)
		if err != nil {
			throw("git pull failed %v", err)
		}
		log("sync git success dst=%v", dst)
	} else {
		log("sync git exists dst=%v ", dst)
	}

	return dst
}
