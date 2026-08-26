# Nature Tests

This document explains how to work with tests in the Nature programming language project.

## Test Structure

Tests are located in `tests/features/` and follow a naming convention: `YYYYMMDD_NN_description.c`

- Each test has a corresponding `.c` file that sets up the test harness
- Test cases are located in `tests/features/cases/` directory
- Some tests have their own subdirectories with multiple `.n` files
- Expected outputs are defined in `.testar` files

## Prerequisites

Before running tests, ensure the runtime library is up to date. If you encounter linking errors like `_global_safepoint` symbol not found, rebuild the runtime:

```bash
cd /path/to/nature
cmake -B build-runtime -S runtime -DCMAKE_BUILD_TYPE=Release
cmake --build build-runtime --target runtime
```

This generates the `lib/darwin_arm64/libruntime.a` (or appropriate platform library) with all necessary symbols.

## Building Tests

Build the entire project including tests:

```bash
cd /path/to/nature/build
cmake ..
make
```

Or rebuild everything from scratch:

```bash
cd /path/to/nature/build
make clean
cmake ..
make -j8
```

## Running Tests

### Run All Tests

```bash
cd build
ctest
```

### Run a Specific Test

```bash
# By test name
ctest -R test_name

# Example: Run selective imports test
ctest -R 20260103_00_selective_imports
```

### Run Tests with Verbose Output

```bash
# See detailed output including compilation and execution
ctest -R test_name -V

# Example with verbose output
ctest -R 20260103_00_selective_imports -V
```

### Run Tests Matching a Pattern

```bash
# Run all tests containing "import" in the name
ctest -R import

# Run all tests from a specific date
ctest -R 20260103
```

### Rerun Failed Tests

```bash
ctest --rerun-failed --output-on-failure
```

## Test Output

Tests compare actual output with expected output defined in `.testar` files:

```
=== test_name
--- main.n
expected output line 1
expected output line 2
...
```

## Common Issues

### Linking Errors

**Problem:** `Undefined symbols for architecture arm64: "_global_safepoint"`

**Solution:** Rebuild the runtime library (see Prerequisites above)

### Test Not Found

**Problem:** Test doesn't appear in ctest list

**Solution:** Rebuild the project to register new tests:
```bash
cd build
cmake ..
make
```

### Build Failures

**Problem:** Compilation errors in test files

**Solution:** Check that all required modules exist in the test's case directory and verify the test's `.c` file is properly configured.

## Creating New Tests

1. Create a test file in `tests/features/`:
   ```bash
   touch tests/features/YYYYMMDD_NN_description.c
   ```

2. Create test case directory and files:
   ```bash
   mkdir -p tests/features/cases/YYYYMMDD_NN_description
   # Add your .n test files
   ```

3. Create expected output file:
   ```bash
   touch tests/features/cases/YYYYMMDD_NN_description.testar
   ```

4. Rebuild to register the test:
   ```bash
   cd build
   cmake ..
   make
   ```

5. Run your new test:
   ```bash
   ctest -R YYYYMMDD_NN_description -V
   ```

## Test Naming Convention

Use the format: `YYYYMMDD_NN_description`
- `YYYYMMDD`: Date (e.g., 20260103 for January 3, 2026)
- `NN`: Sequential number (00, 01, 02, etc.)
- `description`: Brief description (e.g., `selective_imports`)

Example: `20260103_00_selective_imports.c`

## Debugging Tests

To manually test a Nature program without ctest:

```bash
cd tests/features/cases/your_test_directory
/path/to/nature/build/nature build main.n
./main
```

This helps isolate issues in the Nature code vs. the test harness.

## Additional Resources

- Main project README: `../README.md`
- Nature documentation: https://nature-lang.org/docs
- Test cases examples: `tests/features/cases/`
