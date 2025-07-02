# 设置交叉编译默认名称(没有指定编译连链时触发)
if (CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(CMAKE_SYSTEM_NAME darwin)
    message("set CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(CMAKE_SYSTEM_NAME linux)
    message("set CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
endif ()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
    set(CMAKE_SYSTEM_PROCESSOR amd64)
    message("set CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}")
endif ()


if (CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
    set(CMAKE_SYSTEM_PROCESSOR amd64)
    message("set CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}")
endif ()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL aarch64)
    set(CMAKE_SYSTEM_PROCESSOR arm64)
    message("set CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}")
endif ()


# 写入到系统变量中
if (CMAKE_SYSTEM_NAME MATCHES darwin)
    add_definitions(-D_XOPEN_SOURCE=700)
    add_definitions(-D_DARWIN_C_SOURCE)

    add_compile_options(-Wno-format)
    add_compile_options(-Wno-return-type)

    message("add compile def __DARWIN, D_XOPEN_SOURCE, D_DARWIN_C_SOURCE")
    add_compile_definitions(__DARWIN=1)
else ()
    add_definitions(-D_GNU_SOURCE)

    message("add compile def __LINUX, D_GNU_SOURCE, ")
    add_compile_definitions(__LINUX=1)
endif ()

if (CMAKE_SYSTEM_PROCESSOR MATCHES amd64)
    message("add compile def __AMD64")
    add_compile_definitions(__AMD64=1)
endif ()

if (CMAKE_SYSTEM_PROCESSOR MATCHES arm64)
    message("add compile def __ARM64")
    add_compile_definitions(__ARM64=1)
endif ()

if (CMAKE_SYSTEM_PROCESSOR MATCHES riscv64)
    message("add compile def __riscv64")
    add_compile_definitions(__RISCV64=1)
endif ()