set(CMAKE_C_COMPILER cc)
set(CMAKE_CXX_COMPILER c++)

set(CMAKE_C_STANDARD 11)

set(CMAKE_SYSTEM_NAME darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)

set(CMAKE_OSX_ARCHITECTURES "arm64")

set(RUST_TARGET aarch64-apple-darwin)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format -Wno-return-type")
