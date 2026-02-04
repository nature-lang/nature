# Repository Guidelines

## Project Structure & Module Organization
- `src/`: C compiler front/mid/back-end (AST, CFG/SSA/LIR, lowering, semantic analysis).
- `runtime/`: GC, allocator, coroutine, and libc shims; built first into `lib/<platform>/libruntime.a`.
- `std/`: Standard library `.n` modules; mirrors namespaces like `fmt`, `io`, `net`, `time`.
- `nls/`: Rust language server; builds to `nls/target/{debug,release}/nls`.
- `tests/`: C harness in `tests/features/*.c` plus cases in `tests/features/cases/` with `.testar` outputs.
- `include/` and `utils/`: Shared headers and helpers reused by compiler/runtime.
- `build*/`, `release/`, `lib/`: Generated artifacts; keep them out of commits unless intentional.

## Build, Test, and Development Commands
- Build runtime (needed before tests): `cmake -B build-runtime -S runtime -DCMAKE_BUILD_TYPE=Release && cmake --build build-runtime --target runtime`.
- Configure + build compiler/tooling: `cmake -B build -S . && cmake --build build -- -j8` (outputs `build/nature`, `build/npkg`, `build/nls`).
- Fast rebuild: `cmake --build build -- -j8`.
- Full tests: `cd build && ctest`.
- Focused tests: `cd build && ctest -R 20260103_00_selective_imports -V` or patterns like `ctest -R import`.
- NLS only: `cd nls && cargo build --release` (run `cargo fmt` first).

## Coding Style & Naming Conventions
- C/C++: Use `.clang-format` (LLVM base, 4-space indent, no tabs); run `clang-format -i file.c`.
- Rust: `cargo fmt`; Go (npkg/scripts): `gofmt -w`.
- Tests follow `YYYYMMDD_NN_description.c` with matching `tests/features/cases/<name>/` and `.testar` output.
- Source files and identifiers favor `snake_case`; keep braces/spaces as formatter defines.

## Testing Guidelines
- Build runtime + main tree before `ctest`; link errors like `_global_safepoint` usually mean rerun the runtime build.
- New tests: harness in `tests/features/` plus cases in `tests/features/cases/<name>/`; rerun `cmake ..` in `build/`, then `ctest -R <name>`.
- Manual spot checks: `cd tests/features/cases/<dir> && ../../build/nature build main.n && ./main`.
- CI (`.github/workflows/test.yml`) builds on Linux/macOS and runs `ctest`; keep additions deterministic and low-output.

## Commit & Pull Request Guidelines
- Use conventional commits (`feat:`, `fix:`, `chore:`) with concise scopes.
- Before pushing, run `cmake --build build -- -j8` and `cd build && ctest`; call out any skipped tests.
- PRs: summarize the change, note impacted areas (compiler/runtime/std/nls/tests), link issues, and list verification commands; mention new assets or downloads.
- Exclude generated `build*/`, caches, and large binaries; document required external artifacts in README-style notes.
