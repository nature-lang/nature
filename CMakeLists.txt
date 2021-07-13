cmake_minimum_required(VERSION 3.16)
project(nature C)

set(CMAKE_C_STANDARD 99)

#add_executable(nature main.c src/ast/ast.c src/ast/ast.h src/value.h test/test_ast.c src/runtime/runtime.h src/lib/table.h src/lib/table.c src/lir.c src/lir.h src/ssa.h src/ssa.c src/register/allocate.h src/register/interval.h src/runtime/gc.h src/register/interval.c src/lib/list.h src/lib/list.c src/lib/stack.c src/lib/stack.h src/lib/slice.c src/lib/slice.h src/register/allocate.c test/test.c src/register/register.h src/compiler.c src/compiler.h src/ast/symbol.c src/ast/symbol.h src/cfg.c src/cfg.h src/lib/helper.h src/syntax/scanner.h src/syntax/parser.h src/syntax/token.h src/syntax/scanner.c src/lib/error.c src/lib/error.h)
add_executable(nature main.c
        src/syntax/scanner.h src/syntax/scanner.c src/syntax/token.h src/ast/ast.h src/ast/ast.c
        src/lib/error.c src/lib/error.h
        src/value.h src/lib/list.h src/lib/list.c src/syntax/token.c
        src/syntax/parser.c src/syntax/parser.h src/debug/debug.c src/debug/debug.h)

include_directories(.)