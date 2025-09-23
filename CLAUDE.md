# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Nature is a general-purpose programming language and compiler written in C. The project includes a self-hosted compiler, runtime, standard library written in Nature language (.n files), language server (nls), and package manager (npkg).

## Build Commands

### CMake Build (Primary)
```bash
# Debug build with various debug options
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DDEBUG_LOG=ON ..
make

# Release build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# Cross-compilation example
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_OS=linux -DBUILD_ARCH=arm64 ..
```

### Zig Build (Alternative)
```bash
zig build                    # Build nature compiler
zig build run -- build main.n   # Build and run with arguments
```

### Testing
```bash
# Run all tests
cd build && ctest

# Run specific test
cd build && ctest -R test_name

# Run runtime debug test for specific case
RUNTIME_DEBUG_CASE=case_name cmake .. && make test_runtime_debug
```

### Standard Library Testing
Nature test cases are in `tests/features/cases/`. Each test has:
- A `.n` file (Nature source code)
- Optional `.testar` file (test expectations)
- Optional directory with `main.n` for complex tests

## Architecture

### Core Components
- **main.c + cmd/**: Command-line interface and argument parsing
- **src/**: Compiler implementation
  - `syntax/`: Lexer and parser
  - `semantic/`: Type checking and semantic analysis
  - `linear.c`: SSA form and optimization
  - `binary/`: Machine code generation (amd64, arm64, riscv64)
  - `build/`: Build system coordination
- **std/**: Standard library written in Nature language
- **runtime/**: Runtime library in C (GC, coroutines, memory allocator)
- **nls/**: Language server in Rust
- **package/**: Package manager in Go

### Compilation Pipeline
1. Parsing (.n files → AST)
2. Semantic analysis (type checking, symbol resolution)
3. Linear IR generation (SSA form)
4. Machine code generation (direct, no LLVM)
5. Linking (with custom linker or system linker)

### Standard Library Structure
Each std module follows this pattern:
- `std/module_name/main.n`: Main implementation
- `std/module_name/README.md`: Documentation following `std/template.md`
- Import with `import module_name` or `import module_name as alias`

## Development Patterns

### Standard Library Development
- Follow `std/template.md` for documentation format
- Place implementation in `main.n`
- Use existing patterns from `std/fmt/`, `std/json/`, etc.
- Test with cases in `tests/features/cases/`

### Command Line Extensions
- Add new flags in `cmd/root.h` using getopt_long
- Update help text in `main.c`
- Handle new options in the switch statement

### Debugging Options
Available via CMake flags in debug builds:
- `DEBUG_LOG`: General debug output
- `DEBUG_LIR`: Linear IR debug output
- `DEBUG_ASM`: Assembly debug output
- `DEBUG_PARSER`: Parser debug output
- `RUNTIME_DEBUG_LOG`: Runtime debug output

### Cross-Platform Support
Nature supports: linux_amd64, linux_arm64, linux_riscv64, darwin_amd64, darwin_arm64

Build system automatically handles:
- libc selection (musl for Linux, system for macOS)
- Architecture-specific code generation
- Platform-specific linking

## Contributing

### Testing New Features
1. Write Nature code in `tests/features/cases/`
2. Create corresponding C test in `tests/features/`
3. Run `ctest` to validate

### TODO List Reference
See issue #131 for current development priorities including:
- Triple operator grammar support
- Enum grammar support
- Build log improvements (-vv flag)
- Temporary directory cleanup
- Error message improvements (`` instead of '')
- Standard library additions (log module)

### Code Style
- C code: Follow existing style (see `.clang-format`)
- Nature code: Follow patterns in existing std modules
- Error messages: Use descriptive, helpful text