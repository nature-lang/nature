# Nature portable linker

`ld` is the portable C linker used by `nature build`. It currently emits
Darwin arm64 Mach-O executables and static Linux ELF64 executables without
invoking a host linker. Explicit `--ld <path>` remains available as an
external-linker fallback.

The implementation was informed by Zig's Mach-O and ELF linker implementations
at commit `738d2be9d6b6ef3ff3559130c05159ef53336224`. Zig-specific components
(allocator, thread pool, incremental compilation, and ZigObject layers) are
not copied or required here. The wire definitions in `macho_format.h` and
`elf_format.h` are deliberately self-contained so every backend can be
compiled and used on non-native hosts.

Mach-O TBD target matching also follows Zig commit
`13b1050d4c898d472d424f9067d990df12eff3fb` (PR #31673): a Darwin AArch64
link accepts both `arm64-macos` and Xcode 26.4's consolidated
`arm64e-macos` symbol entries without changing the output Mach-O subtype.

The public entry points are declared in `ld.h`.  Callers own the options
object and must call `ld_options_deinit` after linking.  Diagnostics include
the input path and, when available, archive member and symbol context.

The implementation is split by responsibility:

- `ld.c` owns the public options API, flag parsing, context lifetime,
  target-backend dispatch, and atomic executable output shared by Mach-O and
  ELF.
- `ld_input.c` owns checked file reading and thin/fat Mach-O, BSD archive,
  dylib, and `MH_OBJECT` parsing plus library/framework discovery.
- `ld_macho_dylib_paths.c` resolves binary-dylib reexports through `-F`,
  `-L`, sysroots, dylib `LC_RPATH` entries, and linker `-rpath` entries.  It
  expands `@rpath`, `@loader_path`, and `@executable_path`, records every
  checked candidate for diagnostics, and keeps dependency cycles bounded.
- `ld_tapi.c` uses the bundled YAML implementation to parse TAPI v3/v4
  text-based stubs structurally, including multi-document target filtering,
  re-exports, Objective-C names, and weak/absolute/TLV symbol metadata.
- Mach-O `N_INDR` aliases are resolved after archive extraction; relocations and
  dyld bindings use the terminal target symbol, while imported aliases are
  omitted from the duplicate undefined-symbol table entry.
- `ld_macho.c` owns symbol resolution, section layout, arm64 relocations,
  dyld/linkedit generation, and code signing.
- `ld_unwind.c` validates `__LD,__compact_unwind` records and builds the
  final two-level `__TEXT,__unwind_info` table.
- `ld_internal.h` contains shared private state and is not part of the public
  linker API.
- `ld_elf_archive.c` owns checked GNU/BSD archive header and member-name
  decoding, including GNU thin archive traversal and archive-relative external
  member paths. Symbol/name-table payloads remain embedded in a thin archive,
  while an ordinary member advances by its 60-byte header and is read from its
  external path.
- `ld_elf_input.c` owns checked ELF64 REL/RELA object parsing, archive/member
  file lifetime and lazy registration, SHT_GROUP validation, and record-level
  `.eh_frame` parsing. Extended section counts/name-table indexes from section
  zero and
  `SHT_SYMTAB_SHNDX` symbol section indexes are decoded with linked-table,
  entry-count, and bounds validation. `SHF_LINK_ORDER` links are checked for
  bounds, self-links, and dependency cycles. Archive members remain lazy
  until symbol resolution needs them.
- `ld_elf_script.c` parses the GNU library-wrapper script subset used by
  glibc and libm: `OUTPUT_FORMAT`, `OUTPUT_ARCH`, `INPUT`, `GROUP`, nested
  `AS_NEEDED`, and `SEARCH_DIR`. It also owns checked tokenization, quoting,
  comments, and target-format validation; `ld_elf_input.c` resolves the
  resulting recursive paths with sysroot expansion and cycle/depth limits.
- `ld_elf_cie.c` implements Zig-compatible first-winner CIE equivalence using
  exact bytes plus relocation offsets, types, addends, and resolved targets.
- `ld_elf_merge.c` implements Zig-style `SHF_MERGE` entity splitting,
  deterministic exact-byte deduplication, and input-to-output offset mapping.
- `ld_elf_property.c` owns checked ELF64 `.note.gnu.property` parsing,
  per-object GNU property merging, canonical note encoding, and error offsets.
- `ld_elf_relro.c` classifies the writable RELRO prefix and checks its
  file/address ranges, page boundary, and `PT_GNU_RELRO` sizes independently
  from the main ELF layout pass.
- `ld_elf_debug.c` classifies non-allocated DWARF inputs, applies the
  Zig-compatible x86_64, AArch64, and RISC-V64 non-alloc relocation sets,
  handles discarded-COMDAT tombstones, and keeps Debug placement outside all
  load segments.
- `ld_elf_zlib.c` is a self-contained RFC 1950/DEFLATE decoder for
  `SHF_COMPRESSED` DWARF sections. It validates stored, fixed-Huffman, and
  dynamic-Huffman streams, the 32 KiB window, exact output size, and Adler-32
  without depending on host zlib.
- `ld_elf_dynamic.c` owns static-PIE `R_*_RELATIVE`/`R_*_IRELATIVE` ordering
  and encoding plus the minimal closed-world `.dynsym`, `.dynstr`, SysV hash,
  GNU hash, and `.dynamic` metadata.
- `ld_elf.c` owns ELF symbol precedence, fixed-point archive extraction,
  COMDAT selection, static section/segment layout, linker-defined symbols,
  GOT/TLS allocation, unwind-record compaction, deterministic section-header
  and section-name tables, and `ET_EXEC`/static-PIE `ET_DYN` image generation.
- `ld_elf_reloc.c` owns architecture-specific ELF relocation scanning,
  range checking, and instruction patching.
- `ld_elf_rel.c` decodes standard 16-byte `Elf64_Rel` records and recovers
  implicit addends from x86_64 fields, AArch64 instructions, and RISC-V64
  instructions before converting them to the linker's unified explicit-addend
  relocation model.
- `ld_elf_eh_frame.c` owns checked, deterministic `.eh_frame_hdr` sizing,
  FDE search-table sorting, and ELF64 little-endian wire encoding.
- `ld_elf_ifunc.c` encodes the architecture-specific static PLT-GOT entries
  used by GNU IFUNC and `R_*_IRELATIVE` processing.
- `ld_elf_symtab.c` owns host-independent ELF64 symbol encoding, local/global
  ordering, the `.symtab` `sh_info` boundary, and the linked `.strtab`.
- `ld_elf_thunk.c` owns AArch64 branch-range tests, stable group/entry storage,
  and checked `ADRP`/`ADD`/`BR x16` range-extension thunk encoding.
- `ld_elf_riscv_uleb.c` validates and applies RISC-V
  `SET_ULEB128`/`SUB_ULEB128` in relocation order while preserving each
  input field's fixed byte width.
- `ld_elf_riscv_relax.c` plans, maps, and reflows `R_RISCV_ALIGN` padding
  after output addresses are known, preserving symbol and relocation offset
  mappings when bytes are removed.
- `ld_elf_internal.h` contains the ELF input model shared by the ELF backend
  implementation units.

The build driver adds Nature's platform library directory to the `-l` search
paths before parsing user `--ldflags`, so package libraries can be selected by
name as well as by direct archive path.

Mach-O archives are lazy by default. Passing `-ObjC` through `--ldflags`
additionally extracts archive members that define Objective-C classes or
categories, or contain Swift metadata, matching the opt-in behavior of ld64.

The Darwin backend currently emits arm64 `MH_EXECUTE`. The ELF backend emits
little-endian static ELF64 for Linux x86_64, aarch64, and riscv64: `pie=false`
selects the fixed-base `ET_EXEC` path and `pie=true` selects a base-zero,
interpreter-free static PIE `ET_DYN` path. Static PIE remains closed-world:
every ordinary undefined symbol must be satisfied by relocatable objects or
archives, no DSO is loaded, and no `PT_INTERP` or `DT_NEEDED` is emitted.
Those are the complete intended ELF architecture set: the backend does not
implement ELF32, i386, 32-bit Arm, or RV32 targets. Relocation names such as
`R_X86_64_32`, `R_AARCH64_ABS32`, and `R_RISCV_32` describe field widths in
ELF64 objects and do not imply support for a 32-bit target architecture.
All three targets are exercised in an Ubuntu arm64 container; amd64 and
riscv64 outputs are run with QEMU user emulation. Static PIE validation covers
base relocations, global pointers, constructors, TLS, IFUNC/IRELATIVE, and
RELRO on every architecture; AArch64 is additionally exercised with glibc's
`rcrt1.o`. GNU, GNU thin, and BSD
archives are extracted lazily to a symbol fixed point. Thin members use names
relative to the archive directory, including ordinary `../` paths emitted by
GNU `ar`; absolute names remain absolute. The external file's current size is
used rather than requiring it to match a possibly stale `ar_size`, matching
GNU ld behavior. GNU `/offset` fields with the thin-archive trailing slash in
the final name-field byte are accepted. The configured entry symbol is seeded
as a linker-defined strong reference, so an entry point that exists only in an
archive member is extracted as it is by Zig's `LinkerDefined.entry` symbol.
Zig commit `738d2be9` supplies the ordinary archive traversal and name-table
model but has no complete thin archive decoder; the thin wire rules were
therefore checked against GNU ar/ld 2.42 instead of being presented as a direct
Zig translation. GNU linker scripts are intentionally limited to library
wrappers rather than general `SECTIONS` layout programs. For the current
static-only backend, `AS_NEEDED` inputs are registered in their written order
and archives are already extracted only when an unresolved symbol requires a
member; dynamic shared-object retention semantics remain outside the backend's
scope. COMDAT groups use stable first-input
selection, and CIE/FDE records owned by discarded COMDAT code are compacted
before relocation. Equivalent live CIEs across different objects are folded to
the first input CIE, and every affected FDE is rewritten to that canonical
record. A zero-valued `.eh_frame` section-symbol reference remains an anchor
to the combined output section even when its input CIE is deduplicated, as in
Zig's dead-`.eh_frame` symbol handling and GCC `crtbeginT.o`. Eligible
`SHF_MERGE` string and fixed-size sections are split into
entities and deduplicated by exact bytes, with section-symbol addends remapped
to the selected entity. Aggregated output sections retain the maximum
alignment of every contributing input section, including inputs encountered
after the output section was first created. Known compiler section families
such as `.text.*`, `.rodata.*`, `.data.*`, and `.bss.*` are collected under
their canonical output names, while other non-TLS allocated sections retain
their complete input name as in Zig `Elf.initOutputSection`. After those
output sections exist, every allocated section whose name is a valid C
identifier gets Zig-compatible linker-defined `__start_<name>` and
`__stop_<name>` fallbacks. They participate in resolution as weak definitions
and are written to the final symbol table as local/hidden symbols when no
input definition overrides them.
Allocated sections remain live even when `sh_size` is zero. They still receive
an aligned output placement and a stable symbol address, which is required by
GCC's empty `.tm_clone_table` section and matches Zig's zero-sized atom model;
COMDAT losers, non-allocated sections, and `SHF_EXCLUDE` sections remain
discarded by their independent liveness rules.
They point at the section address and address-plus-size, carry the section's
output index in `.symtab`, participate in relocations, and are emitted even
when no input explicitly references them. Missing sections and names which
are not C identifiers remain ordinary undefined symbols. Live FDEs are
indexed by a checked `.eh_frame_hdr`, exposed through `PT_GNU_EH_FRAME` and
the linker-defined `__GNU_EH_FRAME_HDR` symbol, so a runtime unwinder can
discover them without scanning the whole image.
Selected relocatable inputs also participate in GNU property merging. Generic
`UINT32_AND` properties plus AArch64 `FEATURE_1_AND` and x86-64
`FEATURE_1_AND` are intersected across every selected object; an object which
does not carry the property therefore clears it, matching GNU ld and avoiding
incorrectly marking uninstrumented Nature code as IBT, SHSTK, BTI, or PAC
compatible. Generic `UINT32_OR`, x86-64 ISA used/needed, stack-size, and
no-copy-on-protected properties use their ABI-defined OR, maximum, or
presence semantics. Unselected archive members do not participate. The
result is sorted and encoded as at most one allocated `.note.gnu.property`,
with matching `PT_NOTE` and `PT_GNU_PROPERTY` program headers; a zero/empty
merge emits none. Note headers, GNU ownership, descriptor bounds, 8-byte
entry alignment, zero padding, type order, descriptor-local duplicates, and
property data sizes are validated with an input section and byte offset in
diagnostics. Multiple complete notes may occur in one relocatable object
(`glibc`'s x86-64 `errlist-data.o` is a real example); matching GNU ld 2.42,
a repeated type in a later note or property section is validated and ignored
for that object, while distinct objects still use the ABI merge operation.
Zig commit `738d2be9` does not implement GNU property processing, so this
piece follows the GNU ELF ABI and was behavior-checked against GNU ld rather
than being presented as a Zig translation.
The writable segment begins with a GNU RELRO prefix containing initialized
TLS data, preinit/init/fini arrays, `.data.rel.ro`, `.bss.rel.ro`, and the GOT.
The following ordinary writable section starts on the next target page, and a
matching read-only `PT_GNU_RELRO` describes the protected file bytes and the
page-rounded memory range. TLS `SHT_NOBITS` remains loader-only and does not
advance the ordinary load cursor. `.bss.rel.ro` is materialized as zero-filled
`PROGBITS`, both so a static relocation can write its resolved value and so
later file-backed data retains the required `PT_LOAD` offset/address mapping.
Zig commit `738d2be9` carries the `z_relro` option and sorts
`.data.rel.ro`, but does not create `PT_GNU_RELRO`; this behavior therefore
uses Zig's section/TLS layout model while following the GNU ELF ABI and GNU ld
2.42 observations for the actual protection header.
Allocated `SHF_LINK_ORDER` contributions retain their section name and are
stably ordered by the final order of the section named by `sh_link`, including
multi-level dependency chains and stable ties. A zero `sh_link` remains a
valid no-dependency contribution. Discarding an associated section through
COMDAT selection, `SHF_EXCLUDE`, or ordinary allocation filtering propagates
to dependent link-order sections to a fixed point. Pure link-order output
sections retain `SHF_LINK_ORDER` and point `sh_link` at the associated output
section. Zig commit `738d2be9` only formats the `L` flag and has no equivalent
layout implementation, so this behavior follows the ELF gABI and was checked
against GNU ld rather than being described as a Zig translation.
Static AArch64 links also relax the four-instruction
TLSDESC sequence emitted by GCC and Clang for PIC TLS into Zig-style
local-exec `nop`/`movz`/`movk` instructions. The synthetic `.got` has distinct
ordinary, GOTTP, and two-word TLSGD entries: AArch64 and RISC-V initial-exec
and global-dynamic inputs therefore retain their ABI-defined entry contents
instead of being folded into an ordinary address GOT. RISC-V TLSGD and
DTP-relative values include the psABI `TLS_DTV_OFFSET` of `0x800`. AArch64
TLSLE ADD and 8/16/32/64/128-bit load/store relocations are applied with
checked/NC range and alignment handling. Static amd64 links relax compiler
TLSGD, TLSLD, GOTTPOFF, and TLSDESC sequences to local-exec forms when Zig's
static-link rules permit it. Local, non-absolute, non-IFUNC
`GOTPCRELX`/`REX_GOTPCRELX` references are relaxed from indirect `mov`, `call`,
or `jump` forms to direct instructions, and an otherwise unused GOT entry is
suppressed. After layout, any relaxed direct reference outside signed 32-bit
range is changed back to its original indirect form, its GOT entry is restored,
and output layout is repeated to a fixed point. The amd64 relocation layer also
accepts GCC and Clang
large-code-model `PC64`, `GOTPC64`, `GOT64`, `GOTOFF64`, and `PLTOFF64`
objects, with separate GOT-base and per-symbol GOT-entry accounting. Narrow
`R_X86_64_8`/`16`/`32` forms and `SIZE32`/`SIZE64` are checked as ELF64 field
writes. AArch64 `LD64_GOTPAGE_LO15` allocates an ordinary GOT entry and writes
the checked, 8-byte-aligned 15-bit offset from `Page(GOT)`; Zig commit
`738d2be9` scans this relocation but omits its resolve arm, so the final field
encoding follows the AArch64 ELF ABI. Static x86_64, AArch64, and RISC-V64
IFUNC references use `.plt.got`
plus `R_*_IRELATIVE` entries in `.rela.dyn`. The x86_64/AArch64 encoders are
translated from Zig commit `738d2be9`; that revision leaves RISC-V as a TODO,
so its `AUIPC/LD/JALR/NOP` PLT-GOT sequence follows the RISC-V psABI and was
checked against GNU ld 2.42 output.
AArch64 `CALL26` and `JUMP26` use fixed-point 5 MiB placement groups and
group-local range-extension islands when the direct target is outside the
signed 28-bit branch range. Thunk keys include the resolved symbol and
relocation addend, so a non-zero addend is applied to the final thunk target
instead of the branch-to-thunk instruction. Each emitted thunk also
contributes Zig-style local `STT_FUNC` metadata named `<target>$thunk`, so ELF
tools classify the synthesized `ADRP`/`ADD`/`BR` bytes as executable AArch64
code and report a useful target-derived name.

ELF64 `SHT_REL` input uses the gABI 16-byte `Elf64_Rel` wire layout. Field
bounds and instruction opcodes are checked before the implicit addend is
decoded; the downstream linker then handles REL and RELA identically. Zig
commit `738d2be9` lists `SHT_REL` in its object-section switch, but
`preadRelocsAlloc` still reads every such section as 24-byte `Elf64_Rela`, so
the standard REL decoder is a corrective implementation rather than a direct
translation of a complete Zig path. RISC-V `ALIGN`, additive/subtractive
field operations, and subtractive ULEB relocations are rejected in REL form:
their existing field is an independent operand and cannot also be recovered
as one unambiguous implicit addend. Their ordinary RELA forms remain
supported. `R_RISCV_SET_ULEB128` and `R_RISCV_SUB_ULEB128` are each valid on
their own and execute in input relocation order, exactly as Zig 738d2be9's
`writeSetUleb`/`writeSubUleb` path does. The usual same-offset SET/SUB sequence
therefore observes the intermediate fixed-width value, while intervening
relocations are not reordered. Nature additionally validates the existing
ULEB field, preserves its encoded byte length, checks `S +/- A`, and stages
each individual write so malformed input cannot partially update the field.

`LD_DEBUG_NONE` remains the default and drops input `.debug_*` sections while
preserving the normal `.symtab`, `.strtab`, and `.shstrtab`. Callers may set
`debug_mode = LD_DEBUG_DWARF` through the C API to retain, merge, decompress,
and relocate non-allocated DWARF. Output Debug sections are always
uncompressed, have `sh_addr == 0`, follow the loaded image, precede the linker
symbol tables, and never change a load segment, entry point, TLS layout, RELRO
range, or thunk distance. Nature does not yet expose this mode through its
front end or CLI because the language compiler does not yet emit DWARF.

Ordinary dynamically loaded PIE, shared-object input/output, `PT_INTERP`,
`DT_NEEDED`, symbol versioning/interposition, copy relocations, section GC,
ELF32, LTO, and partial-link output remain deliberately out of scope. There is
no dormant GC interface or dependency on Zig's `gc.zig`. The current static
output includes program headers and
deterministic ELF64 section headers plus non-allocated `.symtab`, `.strtab`,
and `.shstrtab` metadata outside every `PT_LOAD` segment. Local symbols precede
globals, resolved globals are name-sorted, and common/TLS/linker-defined
symbols carry their final executable values. Linker-defined fallbacks use
Zig-compatible local/hidden output-symbol binding after weak fallback
resolution, and `__ehdr_start` is associated with the first output section
while retaining the image-base value. Synthetic symbols without an allocated
output section are written as `SHN_UNDEF`, matching Zig's `setOutputSym` path.

The implementation deliberately rejects unsupported input segments and
relocation forms with diagnostics. Out-of-range arm64 calls can use a checked
`__branch_islands` trampoline when the callsite can reach the shared island;
interleaved islands for `__text` larger than the BRANCH26 range remain
reserved. Imported function stubs currently use eager GOT binding, so the
full lazy `__stub_helper`/`__la_symbol_ptr` path is also reserved. The current
Nature Darwin test corpus does not require interleaved islands or lazy binding.
