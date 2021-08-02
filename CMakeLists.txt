cmake_minimum_required(VERSION 3.16)
project(nature C)

set(CMAKE_C_STANDARD 99)

add_executable(nature main.c
        src/syntax/scanner.h src/syntax/scanner.c src/syntax/token.h src/ast.h src/ast.c
        src/lib/error.c src/lib/error.h
        src/lib/list.h src/lib/list.c src/lib/table.h src/lib/table.c
        src/value.h src/symbol.h src/symbol.c
        src/syntax/token.c src/syntax/parser.c src/syntax/parser.h src/debug/debug.c src/debug/debug.h src/semantic/analysis.c src/semantic/analysis.h src/semantic/infer.h src/type.h src/semantic/infer.c src/type.c
        src/compiler.h src/compiler.c src/lir.h src/lir.c src/debug/debug_lir.c src/debug/debug_lir.h
        src/cfg.c src/cfg.h
        src/ssa.h src/ssa.c)

include_directories(.)