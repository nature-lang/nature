package src

import (
	"fmt"
	"io"
	"os"
	"strconv"
	"syscall"
)

func Run() {
	// - 读取 exe path 并解析出其中的 tar.gz(暂存内存或者 tmp 目录都可以)
	extractTgz()

	// - 创建 temps 挂载磁盘，并通过 mount ns 得到一个崭新的空间

	// - 将相关 tar.gz 解压到当前空间中
}

func extractTgz() {
	path, err := os.Executable()
	assertf(err == nil, "exe path err: %v", err)

	// 读取最后 8byte 其中记录了结尾的 size
	fd, err := syscall.Open(path, os.O_RDONLY, 0666)
	assertf(err == nil, "open path %v err: %v", path, err)

	// 使用 seek 读取 fd 对应文件的最后 16 个字节
	_, err = syscall.Seek(fd, -16, io.SeekEnd)
	assertf(err == nil, "seek path %v err: %v", path, err)

	buf := make([]byte, 16)
	length, err := syscall.Read(fd, buf)
	assertf(err == nil, "read path %v err: %v", path, err)

	size, _ := strconv.Atoi(string(buf))

	fmt.Printf("read len: %d, value: %d", length, size)
}
