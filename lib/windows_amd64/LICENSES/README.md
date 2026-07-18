# Windows sysroot license inventory

The release sysroot contains binaries produced from the following projects.
The release job must copy the complete corresponding license text into this
directory before packaging:

- Nature runtime: the distribution-level `LICENSE-MIT` and
  `LICENSE-APACHE`; these are not duplicated in the sysroot.
- LLVM compiler-rt: Apache-2.0 WITH LLVM-exception.
- MinGW-w64 CRT and import definitions: the MinGW-w64 license set from the
  locked Zig distribution's `libc/mingw/COPYING` and per-file notices.
- libuv 1.52.1: libuv `LICENSE` and `LICENSE-extra` for the compiled code.
- Mbed TLS 3.6.4: Apache-2.0.

This inventory file is not a replacement for those license texts. Packaging
must fail if any required text is absent.
