cmake_minimum_required(VERSION 3.22)
project(runtime C)

include(../cmake/cross.cmake)

# release 命令, 在 nature 跟目录下执行
# rm -rf build-runtime &&
# cmake -B build-runtime -S runtime -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/linux-amd64-toolchain.cmake -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_BUILD_TYPE=Release &&
# cmake --build build-runtime --target runtime

set(CMAKE_C_STANDARD 11)

add_compile_definitions(RUNTIME)
message("add compile def RUNTIME")

# 启用汇编支持
ENABLE_LANGUAGE(ASM)

set(LIB_OUTPUT_PREFIX ../lib)

# release 时的编译优化等级
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    # NDEBUG 会屏蔽 assert
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
    #    set(CMAKE_C_FLAGS_RELEASE "-O2")
endif ()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    message("DEBUGF enabled")
    add_compile_definitions(DEBUG)
    #    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O2")
endif ()

include_directories(../include)
include_directories(../)
aux_source_directory(../utils UTILS)
FILE(GLOB_RECURSE RUNTIMES ./*c aco/acosw.S)

message("runtime os: ${CMAKE_SYSTEM_NAME} arch: ${CMAKE_SYSTEM_PROCESSOR}")
add_library(runtime STATIC ${RUNTIMES} ${UTILS})

set_target_properties(runtime PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${LIB_OUTPUT_PREFIX}/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR})

# 链接 libuv
target_link_libraries(runtime uv)


message("runtime register success")
