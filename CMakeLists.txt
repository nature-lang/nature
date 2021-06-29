cmake_minimum_required(VERSION 3.16)
project(nature C)

set(CMAKE_C_STANDARD 99)

add_executable(nature main.c src/ast/ast.c src/ast/ast.h src/value.h test/test_ast.c src/runtime/runtime.h src/old/compiler.c src/old/compiler.h src/old/inst.h
        src/ast/optimize.c src/ast/optimize.h src/lib/table.h src/lib/table.c src/lir.c src/lir.h src/ssa.h src/ssa.c src/register/allocate.h src/register/interval.h src/runtime/gc.h src/register/interval.c src/lib/list.h src/lib/list.c src/lib/stack.c src/lib/stack.h src/lib/slice.c src/lib/slice.h src/register/allocate.c test/test.c src/register/register.h src/compiler.c src/compiler.h src/ast/symbol.c src/ast/symbol.h src/cfg.c src/cfg.h src/lib/helper.h)

include_directories(.)