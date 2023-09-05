package src

import (
	"io"
	"os"
	"path"
	"strconv"
	"syscall"
	"time"
)

func Run() {
	// - 读取 exe path 并解析出其中的 tar.gz(暂存内存或者 tmp 目录都可以)
	tgzBuf := extractTgz()

	// 读取 exe 所在目录作为 mountNs 目标
	exe, err := os.Executable()
	assertf(err == nil, "exe path err: %v", err)
	dir := path.Dir(exe)

	// - 创建 temps 挂载磁盘，并通过 mount ns 得到一个崭新的空间
	mountNs(dir)

	// 尝试写入一个文件到 exe 文件中进行验证
	file := path.Join(dir, "parker.tag.gz")
	err = os.WriteFile(file, tgzBuf, 0755)
	assertf(err == nil, "write file err: %v", err)
	logf("write file success: %s, buf len: %v", file, len(tgzBuf))

	// - 将相关 tar.gz 解压到当前空间中
	time.Sleep(1000 * time.Second)
}

func mountNs(dir string) {
	logf("mountNs dir: %s start", dir)
	err := syscall.Unshare(syscall.CLONE_NEWNS)
	assertf(err == nil, "unshare err: %v", err)

	//    if (mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
	//        perror("mount1");
	//        exit(1);
	//    }
	err = syscall.Mount("none", "/", "", syscall.MS_REC|syscall.MS_PRIVATE, "")
	assertf(err == nil, "mount err: %v", err)

	// mount("tmpfs", "/root/testmount", "tmpfs", 0, NULL)
	err = syscall.Mount("tmpfs", dir, "tmpfs", 0, "")
	assertf(err == nil, "mount err: %v", err)
	logf("mountNs dir: %s success", dir)
}

func extractTgz() []byte {
	exe, err := os.Executable()
	assertf(err == nil, "exe exe err: %v", err)

	// 读取最后 8byte 其中记录了结尾的 size
	fd, err := syscall.Open(exe, os.O_RDONLY, 0666)
	assertf(err == nil, "open exe %v err: %v", exe, err)

	// 使用 seek 读取 fd 对应文件的最后 16 个字节
	_, err = syscall.Seek(fd, -16, io.SeekEnd)
	assertf(err == nil, "seek exe %v err: %v", exe, err)

	sizeBuf := make([]byte, 16)
	_, err = syscall.Read(fd, sizeBuf)
	assertf(err == nil, "read exe %v err: %v", exe, err)
	logf("read exe %v tail 16byte str: %v", exe, string(sizeBuf))

	size, err := strconv.Atoi(string(sizeBuf))
	assertf(err == nil, "read exe %v tail 16byte err: %v", exe, err)
	assertf(size > 0, "read exe %v tail 16byte size is zero", exe)

	_, err = syscall.Seek(fd, -16-int64(size), io.SeekEnd)

	result := make([]byte, size)
	n, err := syscall.Read(fd, result)
	assertf(err == nil && n == size, "read exe %v err: %v", exe, err)

	logf("extractTgz success, buf len: %d, expect size: %d", len(result), size)
	return result
}
