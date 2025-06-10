find_program(CMAKE_C_COMPILER
        NAMES
            riscv64-linux-musl-gcc
            musl-gcc
            cc
        REQUIRED
)

set(CMAKE_EXE_LINKER_FLAGS "-static")

set(CMAKE_SYSTEM_NAME linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(RUST_TARGET riscv64-unknown-linux-musl)
