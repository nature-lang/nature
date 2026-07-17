# Release-sysroot producer toolchain for Nature's windows/amd64 target.
#
# Zig is used only while producing the prebuilt sysroot. `nature build` never
# reads this file and never launches Zig, Clang, LLD, or a system archiver.

find_program(NATURE_ZIG_EXECUTABLE NAMES zig REQUIRED)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR amd64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER "${NATURE_ZIG_EXECUTABLE}")
set(CMAKE_C_COMPILER_ARG1 "cc -target x86_64-windows-gnu")
set(CMAKE_ASM_COMPILER "${NATURE_ZIG_EXECUTABLE}")
set(CMAKE_ASM_COMPILER_ARG1 "cc -target x86_64-windows-gnu")

# CMake expects AR and RANLIB to be standalone executables, whereas Zig
# exposes them as subcommands. Tiny checked-in adapters keep the generated
# archive rules conventional and deterministic.
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zig-ar.sh"
        CACHE FILEPATH "Zig deterministic archiver adapter" FORCE)
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-ranlib.sh"
        CACHE FILEPATH "Zig deterministic ranlib adapter" FORCE)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(NATURE_WINDOWS_DEBUG_FLAGS "-fdebug-compilation-dir=.")
    # CMake's Windows-Clang platform default is `-g` (CodeView). Put the
    # explicit format in the configuration flags so it follows and overrides
    # any platform default rather than being overwritten by it.
    set(CMAKE_C_FLAGS_DEBUG "-gdwarf" CACHE STRING
            "Nature Windows debug C flags" FORCE)
    set(CMAKE_ASM_FLAGS_DEBUG "-gdwarf" CACHE STRING
            "Nature Windows debug ASM flags" FORCE)
else ()
    # Zig cc emits CodeView for COFF unless debug information is explicitly
    # disabled. Embedded compilation directories make release archives depend
    # on the producer work path even after their payload is stripped.
    set(NATURE_WINDOWS_DEBUG_FLAGS "-g0")
endif ()
get_filename_component(NATURE_WINDOWS_SOURCE_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(NATURE_WINDOWS_PATH_FLAGS
        "-ffile-prefix-map=${NATURE_WINDOWS_SOURCE_ROOT}=nature-src -fmacro-prefix-map=${NATURE_WINDOWS_SOURCE_ROOT}=nature-src")
set(CMAKE_C_FLAGS_INIT
        "-fno-lto -fno-sanitize=all -funwind-tables ${NATURE_WINDOWS_DEBUG_FLAGS} ${NATURE_WINDOWS_PATH_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT
        "-fno-lto -fno-sanitize=all -funwind-tables ${NATURE_WINDOWS_DEBUG_FLAGS} ${NATURE_WINDOWS_PATH_FLAGS}")
unset(NATURE_WINDOWS_DEBUG_FLAGS)
unset(NATURE_WINDOWS_PATH_FLAGS)
unset(NATURE_WINDOWS_SOURCE_ROOT)

set(RUST_TARGET x86_64-pc-windows-gnu)
