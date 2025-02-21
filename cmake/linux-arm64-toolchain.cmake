# 按优先级查找可用的编译器
find_program(CMAKE_C_COMPILER 
    NAMES 
        aarch64-linux-musl-gcc 
        musl-gcc 
        cc
    REQUIRED
)
set(CMAKE_EXE_LINKER_FLAGS "-static")
#set(CMAKE_FIND_LIBRARY_SUFFIXES "")

set(CMAKE_SYSTEM_NAME linux)
set(CMAKE_SYSTEM_PROCESSOR arm64)

set(RUST_TARGET aarch64-unknown-linux-musl)