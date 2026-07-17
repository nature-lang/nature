#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
SYSROOT=${WINDOWS_SYSROOT:-$ROOT_DIR/lib/windows_amd64}
NATURE_BIN=${NATURE_BIN:-$ROOT_DIR/build/nature}

if [ ! -x "$NATURE_BIN" ]; then
    echo "windows sysroot: Nature compiler not found: $NATURE_BIN" >&2
    exit 1
fi
if [ ! -d "$SYSROOT" ]; then
    echo "windows sysroot: directory not found: $SYSROOT" >&2
    exit 1
fi

required_files='CAPABILITIES.json
SOURCES.lock
SHA256SUMS
crt2.obj
libruntime.a
libuv.a
libmingw32.lib
compiler_rt.lib
ucrtbase.lib
api-ms-win-crt-conio-l1-1-0.lib
api-ms-win-crt-convert-l1-1-0.lib
api-ms-win-crt-environment-l1-1-0.lib
api-ms-win-crt-filesystem-l1-1-0.lib
api-ms-win-crt-heap-l1-1-0.lib
api-ms-win-crt-locale-l1-1-0.lib
api-ms-win-crt-math-l1-1-0.lib
api-ms-win-crt-multibyte-l1-1-0.lib
api-ms-win-crt-private-l1-1-0.lib
api-ms-win-crt-process-l1-1-0.lib
api-ms-win-crt-runtime-l1-1-0.lib
api-ms-win-crt-stdio-l1-1-0.lib
api-ms-win-crt-string-l1-1-0.lib
api-ms-win-crt-time-l1-1-0.lib
api-ms-win-crt-utility-l1-1-0.lib
kernel32.lib
ntdll.lib
advapi32.lib
user32.lib
shell32.lib
ws2_32.lib
bcrypt.lib
iphlpapi.lib
userenv.lib
psapi.lib
dbghelp.lib
ole32.lib
secur32.lib
crypt32.lib
winmm.lib
version.lib
LICENSES/README.md
LICENSES/COMPILER-RT-LICENSE.txt
LICENSES/LIBUV-LICENSE.txt
LICENSES/LIBUV-LICENSE-extra.txt
LICENSES/MBEDTLS-APACHE-2.0.txt
LICENSES/MINGW-W64-COPYING.txt'

old_ifs=$IFS
IFS='
'
for file in $required_files; do
    if [ ! -f "$SYSROOT/$file" ]; then
        echo "windows sysroot: required file is missing: $file" >&2
        exit 1
    fi
done
IFS=$old_ifs

verify_tmpdir=${WINDOWS_VERIFY_TMPDIR:-$ROOT_DIR}
temp_dir=$(mktemp -d "$verify_tmpdir/nature-windows-verify.XXXXXX")
actual_files=$temp_dir/files
hashed_files=$temp_dir/hashes
checksum_file=$temp_dir/SHA256SUMS
capabilities=$temp_dir/capabilities.json
cleanup() {
    rm -rf "$temp_dir"
}
trap cleanup EXIT HUP INT TERM

(
    cd "$SYSROOT"
    LC_ALL=C find . -type f ! -name SHA256SUMS -print |
        sed 's#^\./##' | LC_ALL=C sort >"$actual_files"
    sed 's/\r$//' SHA256SUMS >"$checksum_file"
    awk '{print $2}' "$checksum_file" | LC_ALL=C sort >"$hashed_files"
)
if ! cmp -s "$actual_files" "$hashed_files"; then
    echo "windows sysroot: SHA256SUMS does not cover the complete file set" >&2
    diff -u "$hashed_files" "$actual_files" >&2 || true
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    (cd "$SYSROOT" && sha256sum -c "$checksum_file")
elif command -v shasum >/dev/null 2>&1; then
    (cd "$SYSROOT" && shasum -a 256 -c "$checksum_file")
else
    echo "windows sysroot: sha256sum or shasum is required" >&2
    exit 1
fi

"$NATURE_BIN" coff-capabilities -o "$capabilities" "$SYSROOT"
if ! cmp -s "$SYSROOT/CAPABILITIES.json" "$capabilities"; then
    echo "windows sysroot: capability manifest is stale" >&2
    exit 1
fi

if ! grep -q '"schema":"nature.windows-sysroot-coff-capabilities.v2"' \
        "$capabilities" ||
   ! grep -q '"hard_unresolved_symbols":\[\]' "$capabilities"; then
    echo "windows sysroot: capability closure contract is missing" >&2
    exit 1
fi

if ! command -v strings >/dev/null 2>&1; then
    echo "windows sysroot: strings is required for build-path verification" >&2
    exit 1
fi
for binary in "$SYSROOT"/*.obj "$SYSROOT"/*.a "$SYSROOT"/*.lib; do
    [ -f "$binary" ] || continue
    if LC_ALL=C strings "$binary" | grep -E \
            '(/tmp/|/private/tmp/|/Users/|/home/|/workspace/|/builds/|nature-zig-cache|CMakeFiles/|-fdebug-compilation-dir=|[A-Za-z]:[/\\])' \
            >/dev/null; then
        echo "windows sysroot: absolute build path found in $(basename "$binary")" >&2
        exit 1
    fi
done

echo "windows sysroot: verified"
