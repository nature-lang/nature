cmake_minimum_required(VERSION 3.16)
project(nature C)

set(CMAKE_C_STANDARD 99)

add_executable(nature main.c src/ast.c src/ast.h src/value.h
        test/test_ast.c src/runtime/runtime.h src/compiler.c src/compiler.h src/inst.h
        src/ast/optimize.c src/ast/optimize.h src/lib/table.h src/lib/table.c src/lir.c src/lir.h src/ssa.h src/ssa.c src/register/allocation.h src/register/intervals.h src/runtime/gc.h src/register/intervals.c src/lib/list.h src/lib/list.c src/lib/stack.c src/lib/stack.h)

include_directories(.)