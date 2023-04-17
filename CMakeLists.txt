cmake_minimum_required(VERSION 3.22)
project(nature C)

set(CMAKE_C_STANDARD 11)

file(READ "VERSION" PROJECT_VERSION)

# 打印当前的项目版本
message("nature version: ${PROJECT_VERSION}")
message("workdir: ${CMAKE_CURRENT_BINARY_DIR}")
set(SRC_DIR "${PROJECT_SOURCE_DIR}/src")

# 设置交叉编译工具链
if (CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(CMAKE_SYSTEM_NAME darwin)
endif ()
if (CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(CMAKE_SYSTEM_NAME linux)
endif ()
if (CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
    set(CMAKE_SYSTEM_PROCESSOR amd64)
endif ()

# 开启通用 DEBUGF 输出
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message("DEBUGF enabled")
    add_compile_definitions(DEBUG)
endif ()

set(SRC_BINARY
        ${SRC_DIR}/binary/opcode/amd64/asm.c
        ${SRC_DIR}/binary/opcode/amd64/asm.h
        ${SRC_DIR}/binary/opcode/amd64/opcode.c
        ${SRC_DIR}/binary/opcode/amd64/opcode.h
        ${SRC_DIR}/binary/elf/elf.h
        ${SRC_DIR}/binary/elf/output.h
        ${SRC_DIR}/binary/elf/output.c
        ${SRC_DIR}/binary/elf/amd64.h
        ${SRC_DIR}/binary/elf/amd64.c
        ${SRC_DIR}/binary/elf/linker.h
        ${SRC_DIR}/binary/elf/linker.c
        ${SRC_DIR}/binary/elf/assembler.h
        ${SRC_DIR}/binary/elf/assembler.c)


set(SRC_NATIVE
        ${SRC_DIR}/native/amd64.c
        ${SRC_DIR}/native/amd64.h)


set(SRC_SYNTAX
        ${SRC_DIR}/syntax/scanner.h
        ${SRC_DIR}/syntax/scanner.c
        ${SRC_DIR}/syntax/token.h
        ${SRC_DIR}/syntax/token.c
        ${SRC_DIR}/syntax/parser.c
        ${SRC_DIR}/syntax/parser.h)

set(SRC_SEMANTIC
        ${SRC_DIR}/semantic/analyser.c
        ${SRC_DIR}/semantic/analyser.h
        ${SRC_DIR}/semantic/infer.h
        ${SRC_DIR}/semantic/infer.c)

set(SRC_DEBUG
        ${SRC_DIR}/debug/debug.c
        ${SRC_DIR}/debug/debug.h
        ${SRC_DIR}/debug/asm.h
        ${SRC_DIR}/debug/asm.c
        ${SRC_DIR}/debug/lir.c
        ${SRC_DIR}/debug/lir.h)

set(UTILS
        ${PROJECT_SOURCE_DIR}/utils/assertf.h
        ${PROJECT_SOURCE_DIR}/utils/ct_list.c
        ${PROJECT_SOURCE_DIR}/utils/ct_list.h
        ${PROJECT_SOURCE_DIR}/utils/custom_links.h
        ${PROJECT_SOURCE_DIR}/utils/type.h
        ${PROJECT_SOURCE_DIR}/utils/type.c
        ${PROJECT_SOURCE_DIR}/utils/slice.c
        ${PROJECT_SOURCE_DIR}/utils/slice.h
        ${PROJECT_SOURCE_DIR}/utils/stack.c
        ${PROJECT_SOURCE_DIR}/utils/stack.h
        ${PROJECT_SOURCE_DIR}/utils/error.c
        ${PROJECT_SOURCE_DIR}/utils/error.h
        ${PROJECT_SOURCE_DIR}/utils/linked.h
        ${PROJECT_SOURCE_DIR}/utils/linked.c
        ${PROJECT_SOURCE_DIR}/utils/table.h
        ${PROJECT_SOURCE_DIR}/utils/table.c
        ${PROJECT_SOURCE_DIR}/utils/helper.h
        ${PROJECT_SOURCE_DIR}/utils/bitmap.h
        ${PROJECT_SOURCE_DIR}/utils/bitmap.c
        ${PROJECT_SOURCE_DIR}/utils/exec.c
        ${PROJECT_SOURCE_DIR}/utils/exec.h)

set(CMD
        ${PROJECT_SOURCE_DIR}/cmd/root.h)

set(SRC
        ${SRC_DIR}/structs.h
        ${SRC_DIR}/ast.h
        ${SRC_DIR}/ast.c
        ${SRC_DIR}/symbol/symbol.h
        ${SRC_DIR}/symbol/symbol.c
        ${SRC_DIR}/compiler.h
        ${SRC_DIR}/compiler.c
        ${SRC_DIR}/lir.h
        ${SRC_DIR}/cfg.c
        ${SRC_DIR}/cfg.h
        ${SRC_DIR}/ssa.h
        ${SRC_DIR}/ssa.c
        ${SRC_DIR}/module.h
        ${SRC_DIR}/module.c
        ${SRC_DIR}/build/config.h
        ${SRC_DIR}/build/config.c
        ${SRC_DIR}/build/build.h
        ${SRC_DIR}/build/build.c
        ${SRC_DIR}/lower/amd64.h
        ${SRC_DIR}/lower/amd64.c
        ${SRC_DIR}/register/amd64.h
        ${SRC_DIR}/register/amd64.c
        ${SRC_DIR}/register/register.h
        ${SRC_DIR}/register/register.c
        ${SRC_DIR}/register/allocate.h
        ${SRC_DIR}/register/allocate.c
        ${SRC_DIR}/register/interval.h
        ${SRC_DIR}/register/interval.c
        ${SRC_DIR}/register/linearscan.h
        ${SRC_DIR}/register/linearscan.c
        ${SRC_DIR}/cross.h)

set(RUNTIME_FILES
        ${PROJECT_SOURCE_DIR}/runtime/structs.h
        ${PROJECT_SOURCE_DIR}/runtime/runtime.h
        ${PROJECT_SOURCE_DIR}/runtime/runtime.c
        ${PROJECT_SOURCE_DIR}/runtime/env.h
        ${PROJECT_SOURCE_DIR}/runtime/env.c
        ${PROJECT_SOURCE_DIR}/runtime/processor.h
        ${PROJECT_SOURCE_DIR}/runtime/processor.c
        ${PROJECT_SOURCE_DIR}/runtime/sizeclass.h
        ${PROJECT_SOURCE_DIR}/runtime/memory.h
        ${PROJECT_SOURCE_DIR}/runtime/memory.c
        ${PROJECT_SOURCE_DIR}/runtime/allocator.h
        ${PROJECT_SOURCE_DIR}/runtime/allocator.c
        ${PROJECT_SOURCE_DIR}/runtime/collector.h
        ${PROJECT_SOURCE_DIR}/runtime/collector.c
        ${PROJECT_SOURCE_DIR}/runtime/builtin/builtin.h
        ${PROJECT_SOURCE_DIR}/runtime/builtin/builtin.c
        ${PROJECT_SOURCE_DIR}/runtime/type/basic.c
        ${PROJECT_SOURCE_DIR}/runtime/type/basic.h
        ${PROJECT_SOURCE_DIR}/runtime/type/hash.h
        ${PROJECT_SOURCE_DIR}/runtime/type/map.h
        ${PROJECT_SOURCE_DIR}/runtime/type/map.c
        ${PROJECT_SOURCE_DIR}/runtime/type/array.h
        ${PROJECT_SOURCE_DIR}/runtime/type/struct.c
        ${PROJECT_SOURCE_DIR}/runtime/type/struct.h
        ${PROJECT_SOURCE_DIR}/runtime/type/set.c
        ${PROJECT_SOURCE_DIR}/runtime/type/set.h
        ${PROJECT_SOURCE_DIR}/runtime/type/tuple.c
        ${PROJECT_SOURCE_DIR}/runtime/type/tuple.h
        ${PROJECT_SOURCE_DIR}/runtime/type/string.h
        ${PROJECT_SOURCE_DIR}/runtime/type/string.c
        ${PROJECT_SOURCE_DIR}/runtime/type/list.h
        ${PROJECT_SOURCE_DIR}/runtime/type/list.c)

set(BUILD_SOURCE_FILES ${SRC} ${CMD} ${UTILS} ${SRC_DEBUG} ${SRC_SEMANTIC} ${SRC_SYNTAX} ${SRC_NATIVE} ${SRC_BINARY})

add_executable(nature main.c ${BUILD_SOURCE_FILES})

target_link_libraries(nature m) # 链接数学计算库
set_target_properties(nature PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/debug/bin)

include_directories(.)

enable_testing()
add_subdirectory(tests)
add_subdirectory(runtime)

#--------------------------------- install ---------------------------------
install(TARGETS nature DESTINATION nature/bin)
install(DIRECTORY lib/ DESTINATION nature/lib)
install(DIRECTORY std/ DESTINATION nature/std)
install(FILES LICENSE DESTINATION nature/)
install(FILES VERSION DESTINATION nature/)


include(cmake/CMackCPack.cmake)
