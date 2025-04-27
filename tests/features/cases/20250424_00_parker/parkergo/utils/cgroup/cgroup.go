package cgroup

import (
	"fmt"
	"os"
	"path"
	"rungo/utils/helper"
	"strconv"
	"strings"
	"syscall"
)

const (
	Version1           = 1
	Version2           = 2
	V1DefaultSubSystem = "freezer"
	ProcsSystem        = "cgroup.procs"
	ParkerDir          = "parker"
)

type Cgroup struct {
	ID              string
	Version         uint8
	CgroupPath      string // v1 使用 freezer cgroup 作为目录
	CgroupProcsPath string
}

func New(id string) (*Cgroup, error) {
	if !helper.PathExists("/sys/fs/cgroup") {
		return nil, fmt.Errorf("cgroup not supported")
	}

	var version uint8
	// - version 探测, 通过判断 /sys/fs/cgroup/cgroup.controllers 文件 判断
	if helper.PathExists("/sys/fs/cgroup/cgroup.controllers") {
		version = Version2
	} else {
		if !helper.PathExists(path.Join("/sys/fs/cgroup", V1DefaultSubSystem)) {
			return nil, fmt.Errorf("cgroup freezer not supported")
		}
		version = Version1
	}

	// - 如果 id 为空则生成 32 位随机字符
	if id == "" {
		id = helper.RandomLetter(32)
	}

	// - 使用 parker/:id 作为 cgroup path
	// v1 /sys/fs/cgroup/freezer/parker/:id
	// v2 /sys/fs/cgroup/parker/:id
	var cgPath string
	var procsPath string
	if version == Version1 {
		cgPath = path.Join("/sys/fs/cgroup", V1DefaultSubSystem, ParkerDir, id)
		procsPath = path.Join(cgPath, ProcsSystem)
	} else {
		cgPath = path.Join("/sys/fs/cgroup", ParkerDir, id)
		procsPath = path.Join(cgPath, ProcsSystem)
	}

	if err := os.MkdirAll(cgPath, 0755); err != nil {
		return nil, fmt.Errorf("failed to create cgroup directory: %w", err)
	}

	// - 初始化 cgroup 并返回
	return &Cgroup{
		ID:              id,
		Version:         version,
		CgroupPath:      cgPath,
		CgroupProcsPath: procsPath,
	}, nil
}

func (c *Cgroup) Register(pid int) error {
	pidString := fmt.Sprintf("%d\n", pid) // 注意：这里添加了换行符
	file, err := os.OpenFile(c.CgroupProcsPath, os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		return fmt.Errorf("failed to open cgroup.procs file: %w", err)
	}
	defer file.Close()

	if _, err := file.WriteString(pidString); err != nil {
		return fmt.Errorf("failed to register PID to cgroup: %w", err)
	}

	return nil
}

func (c *Cgroup) Clear() error {
	// 读取 cgroup path 中的所有 pid
	raw, err := os.ReadFile(c.CgroupProcsPath)
	if err != nil {
		return fmt.Errorf("failed to read PIDs from cgroup: %s", err)
	}

	str := strings.TrimSpace(string(raw))
	if str == "" {
		helper.Logf("no pids in cgroup: %s", c.CgroupProcsPath)
	} else {
		helper.Logf("read pids from cgroup: %s", str)
		pids := strings.Split(str, "\n")
		for _, pidStr := range pids {
			if pidStr == "" {
				continue
			}
			pid, err := strconv.Atoi(pidStr)
			if err != nil {
				return fmt.Errorf("failed to convert PID to integer: %s", err)
			}

			// 强制 kill 进程
			if err := syscall.Kill(pid, syscall.SIGKILL); err != nil {
				return fmt.Errorf("failed to kill PID %d: %s", pid, err)
			}
		}
	}

	// 清理 cgroup 目录
	if err := os.RemoveAll(c.CgroupPath); err != nil {
		return fmt.Errorf("failed to remove cgroup directory: %s", err)
	}

	return nil
}
