package main

import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"rungo/utils/helper"
)

func main() {
	args := os.Args

	if len(args) <= 1 {
		helper.Assertf(false, "args failed")
	}

	execPath, err := os.Executable()
	helper.Assertf(err == nil, "Error getting executable path: %v", err)
	helper.Logf("exec_path: %v", execPath)

	targetName := args[1]

	workdir, err := os.Getwd()
	helper.Assertf(err == nil, "Error getting working directory: %v", err)
	helper.Logf("workdir: %v, target: %v", workdir, targetName)

	targetPath := filepath.Join(workdir, targetName)

	workdir = path.Dir(targetPath)
	targetName = path.Base(targetPath)
	helper.Logf("new workdir: %v, target name: %v", workdir, targetName)

	err = os.WriteFile(".target_name", []byte(targetName), 0755)
	helper.Assertf(err == nil, "Error writing to .target_name: %v", err)

	if _, err := os.Stat(targetPath); os.IsNotExist(err) {
		helper.Assertf(false, "file=%v notfound", targetPath)
	}
	helper.Logf("target_path: %v found", targetPath)

	tgzName := fmt.Sprintf("%v.tar.gz", targetName)
	outputName := fmt.Sprintf("%v-c", targetName)

	files, err := os.ReadDir(workdir)
	helper.Assertf(err == nil, "Error reading directory: %v", err)
	var sources []string

	for _, f := range files {
		name := f.Name()
		if name != tgzName && name != outputName {
			sources = append(sources, name)
		}
	}

	err = tarGzFile(workdir, tgzName, sources)
	helper.Assertf(err == nil, "Error creating tar.gz file: %v", err)
	helper.Logf("encode to tgz_name %v in %v", tgzName, workdir)

	runnerPath := os.Getenv("RUNNER_PATH")
	if runnerPath == "" {
		runnerPath = filepath.Join(filepath.Dir(execPath), "runner")
	}

	if _, err := os.Stat(runnerPath); os.IsNotExist(err) {
		helper.Assertf(false, "runner file=%v notfound", runnerPath)
	}
	helper.Logf("runner_path=%v found", runnerPath)

	outputFd, err := os.OpenFile(outputName, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0755)
	helper.Assertf(err == nil, "Error creating output file: %v", err)
	defer outputFd.Close()

	err = appendFileTo(outputFd, runnerPath)
	helper.Assertf(err == nil, "Error appending runner to output: %v", err)

	err = appendFileTo(outputFd, tgzName)
	helper.Assertf(err == nil, "Error appending tar.gz to output: %v", err)

	tgzSizeStr := fmt.Sprintf("%016d", getFileSize(tgzName))
	_, err = outputFd.WriteString(tgzSizeStr)
	helper.Assertf(err == nil, "Error writing size string to output: %v", err)

	os.Remove(tgzName)
	os.Remove(".target_name")

	helper.Logf("runner %s make successful", filepath.Join(workdir, outputName))

	// - 信息输出
	fmt.Printf("%s\n", outputName)
	fmt.Printf("🍻 parker successful\n")
}

func appendFileTo(dst *os.File, srcPath string) error {
	src, err := os.Open(srcPath)
	if err != nil {
		return err
	}
	defer src.Close()

	_, err = io.Copy(dst, src)
	return err
}

func tarGzFile(workdir, tgzName string, sources []string) error {
	tarGz, err := os.Create(tgzName)
	if err != nil {
		return err
	}
	defer tarGz.Close()

	gw := gzip.NewWriter(tarGz)
	defer gw.Close()

	tw := tar.NewWriter(gw)
	defer tw.Close()

	for _, source := range sources {
		fullPath := filepath.Join(workdir, source)
		err := filepath.Walk(fullPath, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}

			// Create a new header from the FileInfo data
			header, err := tar.FileInfoHeader(info, "")
			if err != nil {
				return err
			}

			// Set the name to the relative path
			relPath, _ := filepath.Rel(workdir, path)
			header.Name = relPath

			// Write the header to the tar
			err = tw.WriteHeader(header)
			if err != nil {
				return err
			}

			// If it's a directory, there's no content to write
			if info.IsDir() {
				return nil
			}

			// Write the file content to the tar
			file, err := os.Open(path)
			if err != nil {
				return err
			}
			defer file.Close()
			_, err = io.Copy(tw, file)
			return err
		})

		if err != nil {
			return err
		}
	}
	return nil
}

func getFileSize(filename string) int64 {
	info, err := os.Stat(filename)
	if err != nil {
		helper.Assertf(false, "Error getting file size: %v", err)
	}
	return info.Size()
}
