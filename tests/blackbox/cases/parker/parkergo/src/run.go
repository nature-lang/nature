package src

import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"github.com/docker/docker/pkg/reexec"
	"io"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"rungo/utils/cgroup"
	"rungo/utils/helper"
	"strconv"
	"syscall"
)

func init() {
	//log.Printf("init start, os.Args = %+v\n", os.Args)
	reexec.Register("runTargetWithFork", runTargetWithFork)
	if reexec.Init() {
		os.Exit(0)
	}
}

func Run(sigChan chan os.Signal) {
	// - 读取 exe path 并解析出其中的 tar.gz(暂存内存或者 tmp 目录都可以)
	tgzBuf := extractTgz()

	// 读取 exe 所在目录作为 mountNs 目标
	exe, err := os.Executable()
	helper.Assertf(err == nil, "exe path err: %v", err)
	workdir := path.Dir(exe)

	// - 创建 temps 挂载磁盘，并通过 mount ns 得到一个崭新的空间
	mountNs(workdir)

	// - 将 tar.gz 写入到目标文件夹
	tgzFile := path.Join(workdir, "parker.tag.gz")
	err = os.WriteFile(tgzFile, tgzBuf, 0644)
	helper.Assertf(err == nil, "write tgzFile err: %v", err)
	helper.Logf("write tgzFile success: %s, buf len: %v", tgzFile, len(tgzBuf))

	// - 进行解压
	err = unTgz(workdir, tgzFile)
	helper.Assertf(err == nil, "unTgz err: %v", err)

	// - 读取需要启动的程序名称
	target, err := readTargetName(workdir)
	helper.Assertf(err == nil, "readTargetName err: %v", err)

	cg, err := cgroup.New("")
	helper.Assertf(err == nil, "cgroup new err: %v", err)

	cmd, pid := runTargetWithCgroup(target, cg.ID)
	helper.Logf("runTargetWithCgroup success, pid: %d", pid)

	cmdDone := make(chan error, 1)
	go func() {
		err := cmd.Wait()
		cmdDone <- err
	}()

	for {
		select {
		case sig := <-sigChan:
			helper.Logf("receive signal: %s", sig.String())

			err = cmd.Process.Signal(sig)
			if err != nil {
				helper.Logf("send signal err: %v", err)
			}
			helper.Logf("signal sync child success")
			continue
		case err := <-cmdDone:
			helper.Logf("cmd done, err: %s", err)
			err = cg.Clear()
			helper.Assertf(err == nil, "cgroup clear err: %v", err)
			goto EXIT
		}
	}

EXIT:
	helper.Logf("exit")
}

// - 初始化一个新的 cgroup
// - 启动 target 进程，并当当前进程的启动参数和环境变量进程到该子进程中
func runTargetWithCgroup(target string, cgroupId string) (*exec.Cmd, int) {
	args := []string{"runTargetWithFork", target, cgroupId}
	args = append(args, os.Args[1:]...)
	cmd := reexec.Command(args...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.SysProcAttr.Cloneflags = syscall.CLONE_NEWNS // 继承父进程的 mount ns
	if err := cmd.Start(); err != nil {
		helper.Assertf(false, "failed to run command: %s", err)
	}

	helper.Logf("start fork success, %+v, %v", cmd.Args, cmd.Process.Pid)

	return cmd, cmd.Process.Pid
}

func runTargetWithFork() {
	helper.Logf("runTargetWithFork start, args: %+v\n", os.Args)

	helper.Assertf(len(os.Args) > 2, "args len: %d exception", len(os.Args))

	// 读取当前
	targetPath := os.Args[1]
	cgroupId := os.Args[2]

	cg, err := cgroup.New(cgroupId)
	helper.Assertf(err == nil, "cgroup new err: %v", err)

	pid := syscall.Getpid()
	helper.Logf("fork start, pid: %d", pid)
	err = cg.Register(pid)
	helper.Assertf(err == nil, "cgroup register err: %v", err)

	args := make([]string, 0)
	if len(os.Args) > 3 {
		args = os.Args[3:]
	}
	// 将配置 exec 中显示的名称(直接使用 target name)
	targetName := path.Base(targetPath)
	args = append([]string{targetName}, args...)
	helper.Logf("will exec, targetPath: %s, args: %+v", targetPath, args)

	if helper.PathExists(targetPath) {
		helper.Logf("targetPath: %s found", targetPath)
	} else {
		helper.Assertf(false, "targetPath: %s not exists, cannot exec", targetPath)
	}

	// 执行子进程
	err = syscall.Exec(targetPath, args, os.Environ())
	helper.Assertf(err == nil, "exec err: %v", err)
}

// .target_name 中包含了需要启动的进程的名称
func readTargetName(workdir string) (string, error) {
	targetFile := path.Join(workdir, ".target_name")
	target, err := os.ReadFile(targetFile)
	if err != nil {
		return "", fmt.Errorf("read target file err: %v", err)
	}

	return path.Join(workdir, string(target)), nil
}

func unTgz(workdir string, tgzFile string) error {
	// 打开 tgz 文件
	f, err := os.Open(tgzFile)
	if err != nil {
		return fmt.Errorf("could not open file: %v", err)
	}
	defer f.Close()

	// 创建一个 gzip reader
	gzr, err := gzip.NewReader(f)
	if err != nil {
		return fmt.Errorf("could not create gzip reader: %v", err)
	}
	defer gzr.Close()

	// 创建一个 tar reader
	tr := tar.NewReader(gzr)

	// 循环读取文件
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break // 文件结束
		}
		if err != nil {
			return fmt.Errorf("could not read tar header: %v", err)
		}

		target := filepath.Join(workdir, hdr.Name)
		switch hdr.Typeflag {
		case tar.TypeDir:
			if _, err := os.Stat(target); err != nil {
				if err := os.MkdirAll(target, 0755); err != nil {
					return fmt.Errorf("could not create directory: %v", err)
				}
			}
		case tar.TypeReg:
			// 创建目标文件
			f, err := os.OpenFile(target, os.O_CREATE|os.O_RDWR, os.FileMode(hdr.Mode))
			if err != nil {
				return fmt.Errorf("could not create target file: %v", err)
			}
			defer f.Close()

			// 将 tar 中的内容写入目标文件
			if _, err := io.Copy(f, tr); err != nil {
				return fmt.Errorf("could not write to target file: %v", err)
			}
		}
	}
	return nil
}

func mountNs(dir string) {
	helper.Logf("mountNs dir: %s start", dir)
	err := syscall.Unshare(syscall.CLONE_NEWNS)
	helper.Assertf(err == nil, "unshare err: %v", err)

	err = syscall.Mount("none", "/", "", syscall.MS_REC|syscall.MS_PRIVATE, "")
	helper.Assertf(err == nil, "mount err: %v", err)

	// mount("tmpfs", "/root/testmount", "tmpfs", 0, NULL)
	err = syscall.Mount("tmpfs", dir, "tmpfs", 0, "")
	helper.Assertf(err == nil, "mount err: %v", err)
	helper.Logf("mountNs dir: %s success", dir)
}

func extractTgz() []byte {
	exe, err := os.Executable()
	helper.Assertf(err == nil, "exe exe err: %v", err)

	// 读取最后 8byte 其中记录了结尾的 size
	fd, err := syscall.Open(exe, os.O_RDONLY, 0666)
	helper.Assertf(err == nil, "open exe %v err: %v", exe, err)

	// 使用 seek 读取 fd 对应文件的最后 16 个字节
	_, err = syscall.Seek(fd, -16, io.SeekEnd)
	helper.Assertf(err == nil, "seek exe %v err: %v", exe, err)

	sizeBuf := make([]byte, 16)
	_, err = syscall.Read(fd, sizeBuf)
	helper.Assertf(err == nil, "read exe %v err: %v", exe, err)
	helper.Logf("read exe %v tail 16byte str: %v", exe, string(sizeBuf))

	size, err := strconv.Atoi(string(sizeBuf))
	helper.Assertf(err == nil, "read exe %v tail 16byte err: %v", exe, err)
	helper.Assertf(size > 0, "read exe %v tail 16byte size is zero", exe)

	_, err = syscall.Seek(fd, -16-int64(size), io.SeekEnd)

	result := make([]byte, size)
	n, err := syscall.Read(fd, result)
	helper.Assertf(err == nil && n == size, "read exe %v err: %v", exe, err)

	helper.Logf("extractTgz success, buf len: %d, expect size: %d", len(result), size)
	return result
}
