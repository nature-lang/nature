#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
VARIANT=${WINDOWS_SYSROOT_VARIANT:-release}
SEED=${WINDOWS_SYSROOT_SEED:-$ROOT_DIR/lib/windows_amd64}
OUTPUT=${WINDOWS_SYSROOT_OUTPUT:-$ROOT_DIR/lib/windows_amd64}
WORK=${WINDOWS_SYSROOT_WORK:-$ROOT_DIR/build-windows-sysroot}
ZIG=${ZIG:-zig}

case "$VARIANT" in
    release) cmake_type=Release ;;
    debug) cmake_type=Debug ;;
    *) echo "windows sysroot: variant must be release or debug" >&2; exit 2 ;;
esac

if [ "$($ZIG version)" != 0.14.1 ]; then
    echo "windows sysroot: Zig 0.14.1 is required" >&2
    exit 1
fi
if [ ! -f "$SEED/SOURCES.lock" ]; then
    echo "windows sysroot: locked CRT/import seed is missing: $SEED" >&2
    exit 1
fi

mkdir -p "$WORK" "$OUTPUT"
normalizer=$WORK/archive-normalize
${CC:-cc} -std=c11 -O2 "$SCRIPT_DIR/archive_normalize.c" -o "$normalizer"
normalize_flag=
[ "$VARIANT" = debug ] && normalize_flag=--keep-dwarf

# The seed contains the locked Zig-produced CRT, compiler-rt, libuv and import
# libraries. Nature runtime is always rebuilt from this checkout.
if [ "$SEED" != "$OUTPUT" ]; then
    for file in "$SEED"/*; do
        base=$(basename "$file")
        [ "$base" = SHA256SUMS ] && continue
        [ "$base" = CAPABILITIES.json ] && continue
        if [ -d "$file" ]; then
            rm -rf "$OUTPUT/$base"
            cp -R "$file" "$OUTPUT/$base"
        else
            cp "$file" "$OUTPUT/$base"
        fi
    done
fi

runtime_build=$WORK/runtime-$VARIANT
cmake -S "$ROOT_DIR/runtime" -B "$runtime_build" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/windows-amd64-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE="$cmake_type"
cmake --build "$runtime_build" --target runtime --parallel "${JOBS:-4}"
runtime_archive=$WORK/lib/windows_amd64/libruntime.a
if [ ! -f "$runtime_archive" ]; then
    echo "windows sysroot: current runtime build did not produce $runtime_archive" >&2
    exit 1
fi
cp "$runtime_archive" "$OUTPUT/libruntime.a"

for archive in "$OUTPUT"/*.a "$OUTPUT"/*.lib; do
    [ -f "$archive" ] || continue
    normalized=$WORK/$(basename "$archive").normalized
    "$normalizer" $normalize_flag "$archive" "$normalized"
    mv "$normalized" "$archive"
done
for object in "$OUTPUT"/*.obj; do
    [ -f "$object" ] || continue
    normalized=$WORK/$(basename "$object").normalized
    "$normalizer" $normalize_flag "$object" "$normalized"
    mv "$normalized" "$object"
done

nature_bin=${NATURE_BIN:-$ROOT_DIR/build/nature}
if [ ! -x "$nature_bin" ]; then
    host_build=$WORK/host
    cmake -S "$ROOT_DIR" -B "$host_build" \
        -DNATURE_BUILD_NPKG=OFF -DNATURE_BUILD_NLS=OFF
    cmake --build "$host_build" --target nature --parallel "${JOBS:-4}"
    nature_bin=$host_build/nature
fi
"$nature_bin" coff-capabilities -o "$OUTPUT/CAPABILITIES.json" "$OUTPUT"

checksum=$OUTPUT/SHA256SUMS
: >"$checksum"
(
    cd "$OUTPUT"
    find . -type f ! -name SHA256SUMS -print | sed 's#^\./##' | LC_ALL=C sort
) | while IFS= read -r file; do
    if command -v sha256sum >/dev/null 2>&1; then
        hash=$(sha256sum "$OUTPUT/$file" | awk '{print $1}')
    else
        hash=$(shasum -a 256 "$OUTPUT/$file" | awk '{print $1}')
    fi
    printf '%s  %s\n' "$hash" "$file" >>"$checksum"
done

WINDOWS_SYSROOT="$OUTPUT" NATURE_BIN="$nature_bin" \
    WINDOWS_VERIFY_TMPDIR="$WORK" "$SCRIPT_DIR/verify.sh"
echo "windows sysroot: built $VARIANT variant at $OUTPUT"
