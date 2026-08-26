# Nature Development Guide

A comprehensive guide for new developers working on the Nature programming language.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Initial Setup](#initial-setup)
- [Building Nature](#building-nature)
- [Running Tests](#running-tests)
- [Developing the NLS (Language Server)](#developing-the-nls-language-server)
- [Common Issues](#common-issues)
- [Development Workflow](#development-workflow)

## Prerequisites

### Required Tools

- **C Compiler**: GCC or Clang
- **CMake**: Version 3.10 or higher
- **Rust**: For building the NLS (install from https://rustup.rs)
- **Go**: For building npkg package manager
- **Git**: For version control

### Platform-Specific Requirements

**macOS:**
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew (if not installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake
```

**Linux:**
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential cmake

# Fedora/RHEL
sudo dnf install gcc gcc-c++ cmake
```

## Initial Setup

1. **Clone the repository:**
   ```bash
   git clone https://github.com/nature-lang/nature.git
   cd nature
   ```

2. **Install Rust (if not already installed):**
   ```bash
   curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
   source $HOME/.cargo/env
   ```

3. **Verify prerequisites:**
   ```bash
   cmake --version
   rustc --version
   go version
   gcc --version  # or clang --version
   ```

## Building Nature

### First-Time Build

1. **Build the runtime library:**
   ```bash
   cmake -B build-runtime -S runtime -DCMAKE_BUILD_TYPE=Release
   cmake --build build-runtime --target runtime
   ```

   This creates platform-specific libraries in `lib/` (e.g., `lib/darwin_arm64/libruntime.a`)

2. **Build the main project:**
   ```bash
   mkdir -p build
   cd build
   cmake ..
   make -j8  # Use -j8 for parallel compilation with 8 cores
   ```

   This builds:
   - `nature` - The compiler binary
   - `npkg` - The package manager
   - `nls` - The language server (in `build/release/nls`)
   - All tests

3. **Verify the build:**
   ```bash
   ./nature --version
   ./npkg --version
   ```

### Incremental Builds

After making changes to the code:

```bash
cd build
make -j8  # Rebuilds only what changed
```

### Clean Build

If you encounter strange issues, do a clean build:

```bash
cd build
make clean
cmake ..
make -j8
```

## Running Tests

Nature uses CTest for running tests. All tests must pass before merging changes.

### Run All Tests

```bash
cd build
ctest
```

### Run Specific Tests

```bash
# By test name
ctest -R test_name

# Examples
ctest -R selective_imports
ctest -R 20260103  # All tests from a specific date
ctest -R import    # All tests with "import" in the name
```

### Run Tests with Verbose Output

```bash
ctest -R test_name -V
```

This shows:
- Compilation output
- Program execution
- Expected vs actual output comparison

### Run Failed Tests Only

```bash
ctest --rerun-failed --output-on-failure
```

### Creating New Tests

1. **Create the test C file:**
   ```bash
   touch tests/features/YYYYMMDD_NN_description.c
   ```

2. **Create the test case directory:**
   ```bash
   mkdir -p tests/features/cases/YYYYMMDD_NN_description
   ```

3. **Add test files:**
   - `main.n` - Your Nature code
   - `*.n` - Additional modules if needed
   - `YYYYMMDD_NN_description.testar` - Expected output

4. **Test harness template (`tests/features/YYYYMMDD_NN_description.c`):**
   ```c
   #include "tests/test.h"
   #include "utils/assertf.h"
   #include "utils/exec.h"
   #include <stdio.h>

   static void test_basic() {
       char *raw = exec_output();
       printf("%s", raw);
       
       assert_string_equal(raw, "Expected output here\n");
   }

   int main(void) {
       TEST_BASIC
   }
   ```

5. **Register and run:**
   ```bash
   cd build
   cmake ..  # Registers the new test
   make
   ctest -R YYYYMMDD_NN_description -V
   ```

### Manual Testing Without CTest

For quick iteration during development:

```bash
cd tests/features/cases/your_test_directory
/path/to/nature/build/nature build main.n
./main  # or ./your_binary_name
```

## Developing the NLS (Language Server)

The NLS (Nature Language Server) provides IDE features like autocomplete, go-to-definition, and error checking.

### NLS Project Structure

```
nls/
├── Cargo.toml          # Rust dependencies
├── src/
│   ├── main.rs         # Entry point
│   ├── project.rs      # Project management
│   └── analyzer/       # Code analysis
│       ├── semantic.rs # Semantic analysis
│       ├── syntax.rs   # Parser
│       └── ...
└── target/
    ├── debug/nls       # Debug build
    └── release/nls     # Release build
```

### Building NLS

**Debug build (for development):**
```bash
cd nls
cargo build
```
Output: `nls/target/debug/nls`

**Release build (optimized):**
```bash
cd nls
cargo build --release
```
Output: `nls/target/release/nls`

**From the main build system:**
```bash
cd build
make nls  # Builds release version to build/release/nls
```

### Testing NLS Changes

#### Method 1: Extension Development Host (Recommended)

1. **Build debug NLS:**
   ```bash
   cd nls
   cargo build
   ```

2. **Update VS Code settings** (`~/.config/Code/User/settings.json` or workspace settings):
   ```json
   {
     "nature.languageServer.path": "/path/to/nature/nls/target/debug/nls"
   }
   ```

3. **Or update installed version:**
   ```bash
   sudo cp nls/target/debug/nls /usr/local/nature/bin/nls
   ```

4. **Restart VS Code or reload window:**
   - Press `Cmd+Shift+P` (macOS) or `Ctrl+Shift+P` (Linux/Windows)
   - Type "Reload Window"

5. **Open Extension Development Host:**
   - In VS Code, open the `nls/` folder
   - Press `F5` to launch Extension Development Host
   - Open a `.n` file to test your changes

#### Method 2: Check Logs

Enable NLS logging to debug:

1. **Set environment variable:**
   ```bash
   export RUST_LOG=debug
   ```

2. **Check VS Code Output:**
   - View → Output
   - Select "Nature Language Server" from dropdown

### Common NLS Development Tasks

**Add new parser feature:**
1. Modify `nls/src/analyzer/syntax.rs`
2. Update AST structures in `nls/src/analyzer/common.rs`
3. Build: `cargo build`
4. Test in Extension Development Host

**Add semantic analysis:**
1. Modify `nls/src/analyzer/semantic.rs`
2. Build: `cargo build`
3. Test: Check error messages in VS Code

**Fix type resolution:**
1. Check `resolve_typedef()` or `analyze_ident()` in `semantic.rs`
2. Build: `cargo build`
3. Test with hover and go-to-definition

### NLS Development Workflow

```bash
# 1. Make changes to NLS code
vim nls/src/analyzer/syntax.rs

# 2. Build debug version
cd nls
cargo build

# 3. Copy to installed location (if needed)
sudo cp target/debug/nls /usr/local/nature/bin/nls

# 4. Reload VS Code window
# Press Cmd+Shift+P → "Reload Window"

# 5. Test your changes
# Open a .n file and verify the feature works
```

### Debugging NLS

**Add debug prints:**
```rust
eprintln!("DEBUG: variable = {:?}", variable);
```

**View debug output:**
- VS Code: View → Output → "Nature Language Server"
- Or check terminal where Extension Development Host was launched

**Build with backtrace:**
```bash
RUST_BACKTRACE=1 cargo build
```

## Common Issues

### Issue: "Undefined symbols" or "_global_safepoint not found"

**Solution:** Rebuild the runtime library
```bash
cmake -B build-runtime -S runtime -DCMAKE_BUILD_TYPE=Release
cmake --build build-runtime --target runtime
```

### Issue: Test not found by CTest

**Solution:** Re-run CMake to register new tests
```bash
cd build
cmake ..
make
```

### Issue: NLS changes not taking effect

**Problem:** VS Code is using the installed version at `/usr/local/nature/bin/nls`, not your development version.

**Solutions:**

1. **Update installed version:**
   ```bash
   sudo cp nls/target/debug/nls /usr/local/nature/bin/nls
   ```

2. **Or configure VS Code to use dev version:**
   ```json
   {
     "nature.languageServer.path": "/path/to/nature/nls/target/debug/nls"
   }
   ```

3. **Reload VS Code window** after updating

### Issue: Cargo build fails

**Solution:** Update Rust toolchain
```bash
rustup update
```

### Issue: Permission denied when copying NLS

**Solution:** Use sudo or change VS Code settings instead
```bash
sudo cp nls/target/debug/nls /usr/local/nature/bin/nls
```

## Development Workflow

### Typical Feature Development

1. **Create a feature branch:**
   ```bash
   git checkout -b feature/my-feature
   ```

2. **Make changes:**
   - Compiler: Edit files in `src/`
   - NLS: Edit files in `nls/src/`
   - Tests: Add files in `tests/features/`

3. **Build and test:**
   ```bash
   # Build everything
   cd build && make -j8
   
   # Test compiler changes
   ctest -R my_test -V
   
   # Test NLS changes
   cd ../nls && cargo build
   # Then test in Extension Development Host
   ```

4. **Run all tests before committing:**
   ```bash
   cd build
   ctest
   ```

5. **Commit and push:**
   ```bash
   git add .
   git commit -m "Add feature X"
   git push origin feature/my-feature
   ```

### Quick Iteration During Development

For fast feedback when developing:

**Compiler changes:**
```bash
cd build
make -j8 && ctest -R relevant_test -V
```

**NLS changes:**
```bash
cd nls
cargo build && sudo cp target/debug/nls /usr/local/nature/bin/nls
# Reload VS Code window
```

**Test a specific Nature program:**
```bash
cd your_test_dir
/path/to/nature/build/nature build main.n && ./main
```

## Additional Resources

- **Main README:** `README.md` - Project overview and installation
- **Test README:** `tests/README.md` - Detailed testing documentation
- **Contributing Guide:** (if available)
- **Documentation:** https://nature-lang.org/docs

## Getting Help

- Check existing issues on GitHub
- Read error messages carefully
- Use verbose test output: `ctest -R test -V`
- Check NLS logs: View → Output → "Nature Language Server"
- Ask for help in project discussions or issues

---

Happy coding! 🌿
