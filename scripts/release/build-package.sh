#!/usr/bin/env bash
set -euo pipefail

target="${1:?usage: build-package.sh <target> <release-tag>}"
release_tag="${2:?usage: build-package.sh <target> <release-tag>}"
smoke_dir=""

cleanup() {
    local status=$?
    if [[ -n "${smoke_dir}" && -d "${smoke_dir}" ]]; then
        rm -rf "${smoke_dir}"
    fi
    return "${status}"
}
trap cleanup EXIT

weekly_regex='^weekly\.[0-9]{4}\.(0[1-9]|[1-4][0-9]|5[0-3])$'
stable_regex='^v0\.[0-9]+\.[0-9]+$'
if [[ "${release_tag}" =~ ${stable_regex} ]]; then
    source_version="$(tr -d '\r\n' < VERSION)"
    if [[ "${source_version}" != "${release_tag}" ]]; then
        echo "VERSION (${source_version}) does not match stable tag (${release_tag})" >&2
        exit 1
    fi
elif [[ ! "${release_tag}" =~ ${weekly_regex} ]]; then
    echo "Release tag must match weekly.YYYY.WW or v0.x.x: ${release_tag}" >&2
    exit 1
fi

case "${target}" in
    linux-amd64)
        expected_os="Linux"
        expected_arch_regex='^(x86_64|amd64)$'
        project_compiler="musl-gcc"
        ;;
    linux-arm64)
        expected_os="Linux"
        expected_arch_regex='^(aarch64|arm64)$'
        project_compiler="musl-gcc"
        ;;
    darwin-amd64)
        expected_os="Darwin"
        expected_arch_regex='^(x86_64|amd64)$'
        project_compiler="clang"
        ;;
    darwin-arm64)
        expected_os="Darwin"
        expected_arch_regex='^(aarch64|arm64)$'
        project_compiler="clang"
        ;;
    *)
        echo "Unsupported release target: ${target}" >&2
        exit 1
        ;;
esac

actual_os="$(uname -s)"
actual_arch="$(uname -m)"
if [[ "${actual_os}" != "${expected_os}" ]] ||
   [[ ! "${actual_arch}" =~ ${expected_arch_regex} ]]; then
    echo "Runner ${actual_os}/${actual_arch} cannot produce ${target}" >&2
    exit 1
fi

for tool in cmake go cargo "${project_compiler}" tar; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "Required tool is missing: ${tool}" >&2
        exit 1
    fi
done

if command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc)"
else
    jobs="$(sysctl -n hw.ncpu)"
fi

runtime_build_dir="build-runtime-${target}-release"
project_build_dir="build-release-${target}"
release_dir="release"
toolchain="${PWD}/cmake/${target}-toolchain.cmake"
lib_target="${target/-/_}"

test -f "${toolchain}"
mkdir -p "${release_dir}"

cmake -S runtime -B "${runtime_build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="${toolchain}"
cmake --build "${runtime_build_dir}" \
    --target runtime \
    --parallel "${jobs}"

cmake -S . -B "${project_build_dir}" \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${project_compiler}" \
    -DNATURE_VERSION_OVERRIDE="${release_tag}" \
    -DCPACK_OUTPUT_FILE_PREFIX="${PWD}/${release_dir}"
cmake --build "${project_build_dir}" --parallel "${jobs}"

test -x "${project_build_dir}/nature"
test -x "${project_build_dir}/npkg"
test -x "${project_build_dir}/release/nls"
test -s "lib/${lib_target}/libruntime.a"

version_output="$("${project_build_dir}/nature" -v)"
reported_version="$(printf '%s\n' "${version_output}" | awk '{print $2}')"
if [[ "${reported_version}" != "${release_tag}" ]]; then
    echo "Binary version (${reported_version}) does not match release tag (${release_tag})" >&2
    exit 1
fi

cmake --build "${project_build_dir}" --target package

archive="${release_dir}/nature-${reported_version}-${target}.tar.gz"
if [[ ! -s "${archive}" ]]; then
    echo "Release archive was not produced: ${archive}" >&2
    exit 1
fi

smoke_dir="$(mktemp -d)"
tar -xzf "${archive}" -C "${smoke_dir}"

package_root="${smoke_dir}/nature"
test -x "${package_root}/bin/nature"
test -x "${package_root}/bin/npkg"
test -x "${package_root}/bin/nls"
test -d "${package_root}/std/builtin"
test -s "${package_root}/lib/${lib_target}/libruntime.a"
test "$(tr -d '\r\n' < "${package_root}/VERSION")" = "${release_tag}"

packaged_version_output="$("${package_root}/bin/nature" -v)"
if [[ "${packaged_version_output}" != *"nature ${release_tag} - release build"* ]]; then
    echo "Unexpected packaged version output: ${packaged_version_output}" >&2
    exit 1
fi

mkdir "${smoke_dir}/compile-test"
printf "fn main() { println('release smoke test') }\n" > "${smoke_dir}/compile-test/main.n"
(
    cd "${smoke_dir}/compile-test"
    NATURE_ROOT="${package_root}" "${package_root}/bin/nature" build main.n
    test "$(./main)" = "release smoke test"
)

echo "Release package verified: ${archive}"
