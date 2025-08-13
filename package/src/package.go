package src

const (
	PackageTypeLib      = "lib"
	PackageTypeBin      = "bin"
	DependencyTypeGit   = "git"
	DependencyTypeLocal = "local"
)

type Dependency struct {
	Type    string `toml:"type"`
	Version string `toml:"version"`
	Url     string `toml:"url"`  // https://github.com/foo/rand
	Path    string `toml:"path"` // ~/Code/custom.1
}

type Package struct {
	Name         string                `toml:"name"`
	Version      string                `toml:"version"`
	Authors      []string              `toml:"authors"`
	Description  string                `toml:"description"`
	License      string                `toml:"license"`
	Type         string                `toml:"type"`
	Path         string                `toml:"path"` // 自定义入口文件
	Dependencies map[string]Dependency `toml:"dependencies"`
}
