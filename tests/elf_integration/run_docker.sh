#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
main_image=${NAX_ELF_INTEGRATION_IMAGE:-nax-riscv-tools:20260713}
amd64_image=${NAX_ELF_AMD64_IMAGE:-nax-riscv-clang-probe:20260713}

docker run --rm \
    --network none \
    -v "$repo_root:/work:ro" \
    -w /work \
    -e 'NAX_ELF_ARCHES=arm64' \
    "$main_image" \
    sh /work/tests/elf_integration/run_in_container.sh

exec docker run --rm \
    --network none \
    -v "$repo_root:/work:ro" \
    -w /work \
    -e 'NAX_ELF_ARCHES=amd64 riscv64' \
    "$amd64_image" \
    sh /work/tests/elf_integration/run_in_container.sh
