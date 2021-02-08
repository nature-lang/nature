cmake_minimum_required(VERSION 3.16)
project(nature C)

set(CMAKE_C_STANDARD 99)

add_executable(nature main.c src/ast.c src/ast.h src/value.h
        test/test_ast.c src/runtime/runtime.h src/compiler.c src/compiler.h src/instruction.h
        src/ast/optimize.c src/ast/optimize.h src/lib/table.h src/lib/table.c)

include_directories(.)