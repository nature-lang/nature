cmake_minimum_required(VERSION 3.16)
project(nature C)

set(CMAKE_C_STANDARD 99)

set(SRC_DIR "${PROJECT_SOURCE_DIR}/src")

set(SRC_ASSEMBLER
        ${SRC_DIR}/assembler/amd64/asm.h
        ${SRC_DIR}/assembler/amd64/asm.c
        ${SRC_DIR}/assembler/amd64/opcode.h
        ${SRC_DIR}/assembler/amd64/opcode.c
        ${SRC_DIR}/assembler/elf/lib_elf.h
        ${SRC_DIR}/assembler/elf/elf.c
        ${SRC_DIR}/assembler/elf/elf.h)

set(SRC_SYNTAX
        ${SRC_DIR}/syntax/scanner.h
        ${SRC_DIR}/syntax/scanner.c
        ${SRC_DIR}/syntax/token.h
        ${SRC_DIR}/syntax/token.c
        ${SRC_DIR}/syntax/parser.c
        ${SRC_DIR}/syntax/parser.h)

set(SRC_SEMANTIC
        ${SRC_DIR}/semantic/analysis.c
        ${SRC_DIR}/semantic/analysis.h
        ${SRC_DIR}/semantic/infer.h
        ${SRC_DIR}/semantic/infer.c
        ${SRC_DIR}/semantic/infer.c)

set(SRC_DEBUG
        ${SRC_DIR}/debug/debug.c
        ${SRC_DIR}/debug/debug.h
        ${SRC_DIR}/debug/debug_lir.c
        ${SRC_DIR}/debug/debug_lir.h)

set(SRC_LIB
        ${SRC_DIR}/lib/error.c
        ${SRC_DIR}/lib/error.h
        ${SRC_DIR}/lib/list.h
        ${SRC_DIR}/lib/list.c
        ${SRC_DIR}/lib/table.h
        ${SRC_DIR}/lib/table.c
        ${SRC_DIR}/lib/helper.h
        ${SRC_DIR}/lib/helper.c)
set(SRC
        ${SRC_DIR}/ast.h
        ${SRC_DIR}/ast.c
        ${SRC_DIR}/value.h
        ${SRC_DIR}/symbol.h
        ${SRC_DIR}/symbol.c
        ${SRC_DIR}/type.h
        ${SRC_DIR}/type.c
        ${SRC_DIR}/compiler.h
        ${SRC_DIR}/compiler.c
        ${SRC_DIR}/lir.h
        ${SRC_DIR}/lir.c
        ${SRC_DIR}/cfg.c
        ${SRC_DIR}/cfg.h
        ${SRC_DIR}/ssa.h
        ${SRC_DIR}/ssa.c src/assembler/amd64/register.h src/assembler/amd64/amd64.h src/assembler/amd64/opcode.c src/assembler/elf/elf.h)

add_executable(nature main.c
        ${SRC}
        ${SRC_LIB}
        ${SRC_SEMANTIC}
        ${SRC_DEBUG}
        ${SRC_SYNTAX}
        ${SRC_ASSEMBLER})

include_directories(.)

enable_testing()
add_subdirectory(test)
