# Nature Windows AMD64 linker

Nature's Windows backend is a native, pure-C COFF/PE linker. It is a third
backend next to the existing ELF and Mach-O linkers; it does not link against
LLVM, LLD, the Windows SDK, or a host linker.

## Target contract

The first supported target is `windows_amd64`. It emits a deterministic
PE32+ console executable with these fixed defaults:

- machine: AMD64
- image base: `0x140000000`
- section alignment: `0x1000`
- file alignment: `0x200`
- entry symbol: `mainCRTStartup`
- subsystem: Windows CUI
- timestamp: zero
- stack reserve/commit: 8 MiB / 64 KiB
- heap reserve/commit: 1 MiB / 4 KiB
- ASLR, high-entropy VA, NX, large-address-aware, and terminal-server-aware
  flags enabled

`nature build --target windows_amd64 main.n` uses the built-in linker by
default and writes `main.exe` unless `-o` is provided. The final-user build
path must not execute Clang, LLD, `link.exe`, MinGW `ld`, Zig, or `ar`.

## Ordinary-link scope

The backend accepts AMD64 COFF objects, BigObj objects, GNU/BSD/MS archives,
short import objects, and the controlled Nature Windows sysroot. It implements
lazy archive extraction, strong/weak/common/import symbols, COMDAT and
associative COMDAT selection, `$` subsection ordering, AMD64 relocations,
imports/IAT/thunks, TLS, `.pdata`/`.xdata`, base relocations, DWARF section
retention, and deterministic map output.

LLVM bitcode and COFF objects containing LTCG data are rejected with a
diagnostic instructing the producer to rebuild with `-fno-lto`.

The first release deliberately does not implement LTO/LTCG, PDB or CodeView
type merging, incremental linking, DLL/export generation, resources,
manifests, delay imports, CFG/load-config rewriting, Authenticode, ICF, PGO,
or non-AMD64 Windows targets.

## LLD provenance boundary

The implementation uses the following source tree only as an algorithm and
differential-testing reference:

```text
llvm-project commit c58ba1cf51d2886994da7e667a05c1bfe4f4396b
```

The selectively translated semantic areas are:

- `lld/COFF/COFFLinkerContext.*`
- `lld/COFF/InputFiles.*`
- `lld/COFF/Symbols.*` and `lld/COFF/SymbolTable.*`
- `lld/COFF/Chunks.*`
- `lld/COFF/MarkLive.*` for COMDAT/associative liveness only
- `lld/COFF/MinGW.*` for sysroot-required GNU compatibility
- `lld/COFF/Writer.*`
- `lld/COFF/MapFile.*`

`LTO.*`, PDB/CodeView/MSF code, the full driver/options layer, ICF and
call-graph sorting, DLL export/resource paths, and non-AMD64 targets are not
translated. LLVM containers and class hierarchies are replaced with Nature's
C containers, arenas, tagged unions, and explicit dispatch. Input parsing is
performed through checked little-endian readers rather than casting untrusted
buffers.

Files derived from LLVM semantics identify the original upstream file and
commit and carry:

```text
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
```

LLVM and Zig source trees under `temps/` are development references and are
not installed or shipped as part of Nature.

## Sysroot contract

`lib/windows_amd64` is a versioned, prebuilt sysroot. Release production may
use the locked Zig/Clang toolchain, but ordinary Nature builds only read the
resulting `.obj`, `.a`, and `.lib` files. Every sysroot object is built for
`x86_64-w64-windows-gnu` with LTO and sanitizers disabled and with unwind
tables enabled.

`SOURCES.lock` records source/tool versions and `SHA256SUMS` authenticates
every shipped artifact. A capability scan is a release gate: a new object
header, relocation, COMDAT mode, directive, import-object form, or unresolved
symbol not supported by the linker makes sysroot production fail.

Third-party runtimes are linked statically. A Nature executable may import
Windows system DLLs and UCRT API-set DLLs, but it must not depend on Nature,
libuv, mbedTLS, `libwinpthread-1.dll`, `libgcc_s*.dll`, or `libstdc++*.dll`.
