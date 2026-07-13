# Nature Darwin linker

`ldd` is the portable C linker used by `nature build` for Darwin arm64
executables.  It emits a thin `MH_EXECUTE` Mach-O image, performs arm64
relocations, builds classic dyld rebase/bind streams, and creates an ad-hoc
linker signature without invoking `ld`, `clang`, or `codesign`.

The implementation was informed by Zig's Mach-O linker implementation at
commit `738d2be9d6b6ef3ff3559130c05159ef53336224`.  Zig-specific components
(allocator, thread pool, incremental compilation, and ZigObject layers) are
not copied or required here.  The format definitions in `macho_format.h` are
deliberately self-contained so this code can be compiled on non-Darwin hosts.

The public entry points are declared in `ldd.h`.  Callers own the options
object and must call `ldd_options_deinit` after linking.  Diagnostics include
the input path and, when available, archive member and symbol context.

The implementation is split by responsibility:

- `ldd.c` owns the public options API, flag parsing, context lifetime, and
  target-backend dispatch.
- `ldd_input.c` owns checked file reading and thin/fat Mach-O, BSD archive,
  TBD, dylib, and `MH_OBJECT` parsing plus library/framework discovery.
- Mach-O `N_INDR` aliases are resolved after archive extraction; relocations and
  dyld bindings use the terminal target symbol, while imported aliases are
  omitted from the duplicate undefined-symbol table entry.
- `ldd_macho.c` owns symbol resolution, section layout, arm64 relocations,
  dyld/linkedit generation, code signing, and final output.
- `ldd_unwind.c` validates `__LD,__compact_unwind` records and builds the
  final two-level `__TEXT,__unwind_info` table.
- `ldd_internal.h` contains shared private state and is not part of the public
  linker API.

The build driver adds Nature's platform library directory to the `-l` search
paths before parsing user `--ldflags`, so package libraries can be selected by
name as well as by direct archive path.

The current native target is Darwin arm64 `MH_EXECUTE`.  Other targets and
universal output remain reserved for later backends.

The implementation deliberately rejects unsupported input segments and
relocation forms with diagnostics. Out-of-range arm64 calls can use a checked
`__branch_islands` trampoline when the callsite can reach the shared island;
interleaved islands for `__text` larger than the BRANCH26 range remain
reserved. Imported function stubs currently use eager GOT binding, so the
full lazy `__stub_helper`/`__la_symbol_ptr` path is also reserved. The current
Nature Darwin test corpus does not require interleaved islands or lazy binding.
