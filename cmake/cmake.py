import os
import subprocess
import shutil


def command(cmd):
    print(f'> {cmd}')
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    stdout, stderr = proc.communicate()
    if stdout:
        print(stdout.decode().strip())
    if stderr:
        print(stderr.decode().strip())


# cwd 必须在项目根目录(判断是否有 VERSION)
if not os.path.exists('VERSION'):
    raise Exception("not in project root dir")

print("WORKDIR: " + os.getcwd())

# 读取 VERSION 中的值并输出
with open('VERSION', 'r') as f:
    version = f.read().strip()

print("VERSION: " + version)

command('mkdir -p ./release')
command('mkdir -p ./lib/linux_amd64')
# command('mkdir -p ./lib/linux_riscv64')
command('mkdir -p ./lib/darwin_amd64')


# 删除 build 目录并重建
def build_dir():
    command('rm -rf ./build')
    command('mkdir ./build')


def cmake_runtime(toolchain):
    if toolchain is None:
        raise Exception("toolchain is None")
    build_dir()
    command('cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/' + toolchain)
    command('cmake --build build --target runtime')
    print("\n")


def cmake_package(toolchain):
    if toolchain is None:
        raise Exception("toolchain is None")
    build_dir()
    command(f'cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/{toolchain}'
            + f' -DCPACK_OUTPUT_FILE_PREFIX={os.getcwd()}/release')
    command('cmake --build build --target package')


cmake_runtime('linux-amd64-toolchain.cmake')
# cmake_runtime('linux-riscv64-toolchain.cmake')

cmake_package('linux-amd64-toolchain.cmake')
cmake_package('darwin-amd64-toolchain.cmake')

command("rm -rf ./build")
print("build all successful")
