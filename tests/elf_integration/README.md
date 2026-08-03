# ELF64 external-tool integration tests

This directory contains reproducible, opt-in integration coverage for the
portable ELF linker. It complements the byte-level `test_ld` CTest with real
compiler-produced objects and external format/execution checks.

The default runner mounts the source tree read-only and uses two existing
arm64-hosted tools images: `nature-riscv-tools:20260713` for AArch64 and
`nature-riscv-clang-probe:20260713` for x86_64/RISC-V64 Clang fixtures. The
second image also contains the RISC-V GCC/binutils and QEMU tools used by the
Debug and execution checks:

```sh
tests/elf_integration/run_docker.sh
```

Override the image without editing the script:

```sh
NATURE_ELF_INTEGRATION_IMAGE=my-arm-riscv-tools:tag \
NATURE_ELF_AMD64_IMAGE=my-clang-tools:tag \
    tests/elf_integration/run_docker.sh
```

The suite builds all generated files under a container-local `mktemp`
directory. System linkers are used only to compile relocatable fixtures and
locate static CRT/runtime archives. Every tested executable is linked by the
Nature `ld_link()` C API.

Coverage includes:

- x86_64, AArch64, and RISC-V64 `-g` and `-gz=zlib` objects;
- `readelf --debug-dump=info`, decoded line tables, and `addr2line`;
- `LD_DEBUG_NONE` dropping input Debug sections;
- x86_64, AArch64, and RISC-V64 freestanding static PIE execution with global
  pointers, constructors, TLS, `R_*_RELATIVE`, and IFUNC/`R_*_IRELATIVE`;
- native AArch64 execution when the container architecture is AArch64, with
  QEMU user mode used only as a cross-host fallback;
- glibc `rcrt1.o` and C++ exception static PIE for each architecture whose
  compiler, static runtime, and QEMU runner are present in the image.

Missing optional cross CRT/runtime components are reported as explicit
`SKIP` lines. Missing tools required by the core Debug and freestanding PIE
matrix are failures.
