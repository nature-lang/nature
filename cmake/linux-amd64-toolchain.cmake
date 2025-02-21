find_program(CMAKE_C_COMPILER
        NAMES
        x86_64-linux-musl-gcc
        musl-gcc
        cc
        REQUIRED
)

set(CMAKE_EXE_LINKER_FLAGS "-static")

set(CMAKE_SYSTEM_NAME linux)
set(CMAKE_SYSTEM_PROCESSOR amd64)

set(RUST_TARGET x86_64-unknown-linux-musl)