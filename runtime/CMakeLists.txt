cmake_minimum_required(VERSION 3.22)

project(runtime C)

include(../cmake/cross.cmake)

# release 命令, 在 nature 跟目录下执行
# rm -rf build-runtime &&
# cmake -B build-runtime -S runtime -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/linux-amd64-toolchain.cmake -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_BUILD_TYPE=Release &&
# cmake --build build-runtime --target runtime

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

add_compile_definitions(RUNTIME)
add_compile_definitions(LLHTTP_STATIC_LIB)
message("add compile def RUNTIME")

# 启用汇编支持
ENABLE_LANGUAGE(ASM)

set(LIB_OUTPUT_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../lib")

# release 时的编译优化等级
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    # NDEBUG 会屏蔽 assert
    set(CMAKE_C_FLAGS_RELEASE "-O3 -fno-omit-frame-pointer -DNDEBUG")
endif ()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    message("DEBUGF enabled")
    # runtime debug 标志
    # add_compile_definitions(RUNTIME_DEBUG_LOG)
    add_compile_definitions(TEST_MODE)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O2 -fno-omit-frame-pointer")
endif ()

include_directories(mbedtls/include)
include_directories(../include)
include_directories(../)

aux_source_directory(../utils UTILS)
FILE(GLOB_RECURSE RUNTIMES ./*c aco/acosw.S)

message("runtime os: ${CMAKE_SYSTEM_NAME} arch: ${CMAKE_SYSTEM_PROCESSOR}")
add_library(runtime STATIC ${RUNTIMES} ${UTILS})

# Nature-generated code keeps the current coroutine in a dedicated register.
# A callee-saved register may still be used temporarily by C code before that
# code returns, so reserve it while building runtime code that can call back
# into Nature.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|riscv64)$")
    target_compile_options(runtime PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-ffixed-x27>)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "amd64")
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-ffixed-r14" NATURE_C_COMPILER_HAS_FIXED_R14)

    if (NATURE_C_COMPILER_HAS_FIXED_R14)
        target_compile_options(runtime PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-ffixed-r14>)
    elseif (CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Older Clang releases, including current Apple Clang and Zig cc,
        # expose the same reservation through an LLVM backend option.
        target_compile_options(runtime PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-mllvm>
                $<$<COMPILE_LANGUAGE:C>:-reserve-regs-for-regalloc=R14>)
    else ()
        message(FATAL_ERROR
                "The ${CMAKE_C_COMPILER_ID} C compiler cannot reserve r14")
    endif ()
endif ()

set_target_properties(runtime PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${LIB_OUTPUT_PREFIX}/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR})

# 链接 libuv
target_link_libraries(runtime uv)

message("runtime register success")
