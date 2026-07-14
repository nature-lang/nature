#!/bin/sh
set -eu

root=${NATURE_ROOT:-/work}
jobs=${NATURE_JOBS:-8}
arches=${NATURE_ELF_ARCHES:-"amd64 arm64 riscv64"}
work=$(mktemp -d /tmp/nature-elf-integration.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM

require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required integration tool: $1" >&2
        exit 1
    fi
}

runner_for_arch() {
    case "$1" in
        amd64)
            case "$(uname -m)" in
                x86_64|amd64) echo env ;;
                *) echo qemu-x86_64-static ;;
            esac
            ;;
        arm64)
            case "$(uname -m)" in
                aarch64|arm64) echo env ;;
                *) echo qemu-aarch64-static ;;
            esac
            ;;
        riscv64)
            case "$(uname -m)" in
                riscv64) echo env ;;
                *) echo qemu-riscv64-static ;;
            esac
            ;;
    esac
}

for tool in cmake cc readelf nm addr2line file; do
    require_tool "$tool"
done

cmake -B "$work/build" -S "$root" -DBUILD_TESTING=ON >/dev/null 2>&1
cmake --build "$work/build" --target nature_common_test -- -j"$jobs"
cc -std=gnu11 -O2 -I"$root" -I"$root/include" \
    "$root/tests/elf_integration/link_driver.c" \
    "$work/build/tests/libnature_common_test.a" -lm \
    -o "$work/ld-driver"

compile_debug_object() {
    debug_arch=$1
    debug_compression=$2
    debug_object=$3
    debug_compression_flag=
    if [ "$debug_compression" = zlib ]; then
        debug_compression_flag=-gz=zlib
    fi
    case "$debug_arch" in
        amd64)
            require_tool clang
            clang --target=x86_64-linux-gnu -O0 -g \
                $debug_compression_flag \
                -ffreestanding -fno-pic -fno-pie -c \
                "$root/tests/elf_integration/debug_fixture.c" \
                -o "$debug_object"
            ;;
        arm64)
            require_tool aarch64-linux-gnu-gcc
            aarch64-linux-gnu-gcc -O0 -g $debug_compression_flag \
                -ffreestanding -fno-pic -fno-pie -c \
                "$root/tests/elf_integration/debug_fixture.c" \
                -o "$debug_object"
            ;;
        riscv64)
            require_tool riscv64-linux-gnu-gcc
            riscv64-linux-gnu-gcc -O0 -g $debug_compression_flag \
                -mno-relax \
                -ffreestanding -fno-pic -fno-pie -c \
                "$root/tests/elf_integration/debug_fixture.c" \
                -o "$debug_object"
            ;;
        *)
            echo "unsupported debug fixture architecture: $debug_arch" >&2
            exit 1
            ;;
    esac
}

verify_debug_output() {
    debug_label=$1
    debug_output=$2
    readelf -hW "$debug_output" | grep -Eq 'Class:[[:space:]]+ELF64'
    readelf -SW "$debug_output" | grep -Eq '\.debug_info[[:space:]]'
    readelf -SW "$debug_output" | grep -Eq '\.debug_line[[:space:]]'
    if readelf -SW "$debug_output" | grep '\.debug_' | \
        grep -Eq '[[:space:]]C[[:space:]]'; then
        echo "$debug_label output retained SHF_COMPRESSED" >&2
        exit 1
    fi
    readelf --debug-dump=info "$debug_output" >/dev/null
    readelf --debug-dump=decodedline "$debug_output" >/dev/null
    debug_address=$(nm -n "$debug_output" |
        awk '$3 == "add" { print $1; exit }')
    if [ -z "$debug_address" ]; then
        echo "$debug_label output has no add symbol" >&2
        exit 1
    fi
    debug_location=$(addr2line -e "$debug_output" -f -C \
        "$debug_address" | sed -n '2p')
    case "$debug_location" in
        debug_fixture.c:100|*/debug_fixture.c:100) ;;
        *)
            echo "$debug_label addr2line returned '$debug_location', expected debug_fixture.c:100" >&2
            exit 1
            ;;
    esac
}

echo '== DWARF -g / -gz=zlib =='
for arch in $arches; do
    debug_runner=$(runner_for_arch "$arch")
    require_tool "$debug_runner"
    debug_plain_stdout=
    debug_zlib_stdout=
    for compression in plain zlib; do
        object="$work/debug-$arch-$compression.o"
        output="$work/debug-$arch-$compression"
        compile_debug_object "$arch" "$compression" "$object"
        if [ "$compression" = zlib ] &&
           ! readelf -SW "$object" | grep '\.debug_' | \
                grep -Eq '[[:space:]]C[[:space:]]'; then
            echo "$arch -gz=zlib input contains no SHF_COMPRESSED debug section" >&2
            exit 1
        fi
        "$work/ld-driver" "$arch" exec dwarf "$output" "$object"
        verify_debug_output "$arch/$compression" "$output"
        if [ "$compression" = plain ]; then
            debug_plain_stdout=$("$debug_runner" "$output")
        else
            debug_zlib_stdout=$("$debug_runner" "$output")
        fi
    done
    "$work/ld-driver" "$arch" exec none "$work/nodebug-$arch" \
        "$work/debug-$arch-plain.o"
    if readelf -SW "$work/nodebug-$arch" | grep -q '\.debug_'; then
        echo "$arch LD_DEBUG_NONE retained a debug section" >&2
        exit 1
    fi
    debug_none_stdout=$("$debug_runner" "$work/nodebug-$arch")
    if [ "$debug_plain_stdout" != "$debug_zlib_stdout" ] ||
       [ "$debug_plain_stdout" != "$debug_none_stdout" ]; then
        echo "$arch Debug/None executable stdout differs" >&2
        exit 1
    fi
    echo "PASS debug $arch"
done

verify_static_pie() {
    pie_label=$1
    pie_output=$2
    pie_runner=$3
    pie_relative_name=$4
    pie_irelative_name=$5
    file "$pie_output" | grep -Fq 'pie executable'
    readelf -hW "$pie_output" | grep -Eq \
        'Type:[[:space:]]+DYN \(Position-Independent Executable file\)'
    if readelf -lW "$pie_output" | grep -q INTERP; then
        echo "$pie_label static PIE unexpectedly contains PT_INTERP" >&2
        exit 1
    fi
    readelf -lW "$pie_output" | grep -q DYNAMIC
    readelf -lW "$pie_output" | grep -q GNU_RELRO
    if readelf -dW "$pie_output" | grep -q NEEDED; then
        echo "$pie_label static PIE unexpectedly contains DT_NEEDED" >&2
        exit 1
    fi
    readelf -dW "$pie_output" | grep -q 'Flags: PIE'
    readelf -rW "$pie_output" | grep -q "$pie_relative_name"
    readelf -rW "$pie_output" | grep -q "$pie_irelative_name"
    "$pie_runner" "$pie_output"
}

echo '== freestanding static PIE execution =='
for arch in $arches; do
    case "$arch" in
        amd64)
            require_tool qemu-x86_64-static
            require_tool clang
            clang --target=x86_64-linux-gnu -O2 -g -fPIE -ffreestanding \
                -fno-stack-protector -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -c "$root/tests/elf_integration/static_pie_selfreloc.c" \
                -o "$work/selfreloc-amd64.o"
            clang --target=x86_64-linux-gnu -O2 -g -fPIE -ffreestanding \
                -fno-stack-protector -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -c "$root/tests/elf_integration/static_pie_ifunc.c" \
                -o "$work/ifunc-amd64.o"
            "$work/ld-driver" amd64 pie dwarf "$work/static-pie-amd64" \
                "$work/selfreloc-amd64.o" "$work/ifunc-amd64.o"
            verify_static_pie amd64 "$work/static-pie-amd64" \
                qemu-x86_64-static R_X86_64_RELATIVE R_X86_64_IRELATIVE
            echo 'PASS static PIE amd64'
            ;;
        riscv64)
            require_tool qemu-riscv64-static
            require_tool clang
            clang --target=riscv64-linux-gnu -O2 -g -fPIE \
                -ffreestanding -mno-relax \
                -fno-stack-protector -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -c "$root/tests/elf_integration/static_pie_selfreloc.c" \
                -o "$work/selfreloc-riscv64.o"
            clang --target=riscv64-linux-gnu -O2 -g -fPIE \
                -ffreestanding -mno-relax \
                -fno-stack-protector -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -c "$root/tests/elf_integration/static_pie_ifunc.c" \
                -o "$work/ifunc-riscv64.o"
            "$work/ld-driver" riscv64 pie dwarf \
                "$work/static-pie-riscv64" \
                "$work/selfreloc-riscv64.o" "$work/ifunc-riscv64.o"
            verify_static_pie riscv64 "$work/static-pie-riscv64" \
                qemu-riscv64-static R_RISCV_RELATIVE R_RISCV_IRELATIVE
            echo 'PASS static PIE riscv64'
            ;;
        arm64)
            arm64_runner=$(runner_for_arch arm64)
            require_tool "$arm64_runner"
            require_tool aarch64-linux-gnu-gcc
            aarch64-linux-gnu-gcc -O2 -g -fPIE -ffreestanding \
                -ftls-model=local-exec -fno-stack-protector \
                -fno-unwind-tables -fno-asynchronous-unwind-tables \
                -c "$root/tests/elf_integration/static_pie_selfreloc.c" \
                -o "$work/selfreloc-arm64.o"
            aarch64-linux-gnu-gcc -O2 -g -fPIE -ffreestanding \
                -fno-stack-protector -fno-unwind-tables \
                -fno-asynchronous-unwind-tables \
                -c "$root/tests/elf_integration/static_pie_ifunc.c" \
                -o "$work/ifunc-arm64.o"
            "$work/ld-driver" arm64 pie dwarf \
                "$work/static-pie-arm64" \
                "$work/selfreloc-arm64.o" "$work/ifunc-arm64.o"
            verify_static_pie arm64 "$work/static-pie-arm64" \
                "$arm64_runner" R_AARCH64_RELATIVE R_AARCH64_IRELATIVE
            echo 'PASS static PIE arm64'
            ;;
    esac
done

compiler_for_arch() {
    case "$1" in
        amd64) echo x86_64-linux-gnu-gcc ;;
        arm64) echo aarch64-linux-gnu-gcc ;;
        riscv64) echo riscv64-linux-gnu-gcc ;;
    esac
}

cxx_for_arch() {
    case "$1" in
        amd64) echo x86_64-linux-gnu-g++ ;;
        arm64) echo aarch64-linux-gnu-g++ ;;
        riscv64) echo riscv64-linux-gnu-g++ ;;
    esac
}

run_glibc_static_pie() {
    glibc_arch=$1
    glibc_cc=$(compiler_for_arch "$glibc_arch")
    glibc_cxx=$(cxx_for_arch "$glibc_arch")
    glibc_runner=$(runner_for_arch "$glibc_arch")
    if ! command -v "$glibc_cc" >/dev/null 2>&1 ||
       ! command -v "$glibc_cxx" >/dev/null 2>&1 ||
       ! command -v "$glibc_runner" >/dev/null 2>&1; then
        echo "SKIP glibc/C++ static PIE $glibc_arch: cross compiler or runner missing"
        return
    fi
    glibc_rcrt1=$($glibc_cc -print-file-name=rcrt1.o)
    if [ "$glibc_rcrt1" = rcrt1.o ] || [ ! -f "$glibc_rcrt1" ]; then
        echo "SKIP glibc/C++ static PIE $glibc_arch: rcrt1.o missing"
        return
    fi
    glibc_crti=$($glibc_cc -print-file-name=crti.o)
    glibc_crtbegin=$($glibc_cc -print-file-name=crtbeginS.o)
    glibc_crtend=$($glibc_cc -print-file-name=crtendS.o)
    glibc_crtn=$($glibc_cc -print-file-name=crtn.o)
    glibc_libc=$($glibc_cc -print-file-name=libc.a)
    glibc_libm=$($glibc_cc -print-file-name=libm.a)
    glibc_libgcc=$($glibc_cc -print-libgcc-file-name)
    glibc_libgcc_eh=$($glibc_cc -print-file-name=libgcc_eh.a)
    glibc_libstdcxx=$($glibc_cxx -print-file-name=libstdc++.a)
    for glibc_input in "$glibc_crti" "$glibc_crtbegin" \
        "$glibc_crtend" "$glibc_crtn" "$glibc_libc" "$glibc_libm" \
        "$glibc_libgcc" "$glibc_libgcc_eh" "$glibc_libstdcxx"; do
        if [ ! -f "$glibc_input" ]; then
            echo "SKIP glibc/C++ static PIE $glibc_arch: missing $glibc_input"
            return
        fi
    done

    "$glibc_cc" -O2 -g -fPIE -c \
        "$root/tests/elf_integration/static_pie_glibc.c" \
        -o "$work/glibc-$glibc_arch.o"
    "$work/ld-driver" "$glibc_arch" pie dwarf \
        "$work/glibc-$glibc_arch" \
        "$glibc_rcrt1" "$glibc_crti" "$glibc_crtbegin" \
        "$work/glibc-$glibc_arch.o" "$glibc_libgcc" \
        "$glibc_libgcc_eh" "$glibc_libc" "$glibc_libgcc" \
        "$glibc_libgcc_eh" "$glibc_crtend" "$glibc_crtn"
    verify_static_pie "$glibc_arch/glibc" \
        "$work/glibc-$glibc_arch" "$glibc_runner" \
        "$(case "$glibc_arch" in amd64) echo R_X86_64_RELATIVE ;; arm64) echo R_AARCH64_RELATIVE ;; *) echo R_RISCV_RELATIVE ;; esac)" \
        "$(case "$glibc_arch" in amd64) echo R_X86_64_IRELATIVE ;; arm64) echo R_AARCH64_IRELATIVE ;; *) echo R_RISCV_IRELATIVE ;; esac)"

    "$glibc_cxx" -O2 -g -fPIE -fexceptions -c \
        "$root/tests/elf_integration/cpp_exception.cpp" \
        -o "$work/cpp-$glibc_arch.o"
    "$work/ld-driver" "$glibc_arch" pie dwarf \
        "$work/cpp-$glibc_arch" \
        "$glibc_rcrt1" "$glibc_crti" "$glibc_crtbegin" \
        "$work/cpp-$glibc_arch.o" "$glibc_libstdcxx" "$glibc_libm" \
        "$glibc_libgcc" "$glibc_libgcc_eh" "$glibc_libc" \
        "$glibc_libgcc" "$glibc_libgcc_eh" "$glibc_crtend" \
        "$glibc_crtn"
    verify_static_pie "$glibc_arch/C++" "$work/cpp-$glibc_arch" \
        "$glibc_runner" \
        "$(case "$glibc_arch" in amd64) echo R_X86_64_RELATIVE ;; arm64) echo R_AARCH64_RELATIVE ;; *) echo R_RISCV_RELATIVE ;; esac)" \
        "$(case "$glibc_arch" in amd64) echo R_X86_64_IRELATIVE ;; arm64) echo R_AARCH64_IRELATIVE ;; *) echo R_RISCV_IRELATIVE ;; esac)"
    echo "PASS glibc/C++ static PIE $glibc_arch"
}

echo '== glibc rcrt1 / C++ exception static PIE =='
for arch in $arches; do
    run_glibc_static_pie "$arch"
done

echo 'ELF integration suite passed'
