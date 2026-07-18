include(CTest)

# runtime test
ENABLE_LANGUAGE(ASM)
FILE(GLOB_RECURSE RUNTIMES ../runtime/*c ../runtime/aco/acosw.S)

if (CMAKE_SYSTEM_NAME STREQUAL "darwin")
    set(LIBC_STATIC "")
else ()
    set(LIBC_STATIC "${CMAKE_SOURCE_DIR}/lib/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}/libc.a")
endif ()

message("LIBC_STATIC: ${LIBC_STATIC}")

# 创建静态库，避免重复编译
add_library(nature_common_test STATIC ${SRC} ${CMD} ${UTILS})
target_include_directories(nature_common_test PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR})
target_compile_definitions(nature_common_test PRIVATE TEST_MODE)

add_executable(test_arm64_encoding test_arm64_encoding.c)
target_link_libraries(test_arm64_encoding nature_common_test m)
add_test(test_arm64_encoding test_arm64_encoding)

add_executable(test_amd64_encoding test_amd64_encoding.c)
target_link_libraries(test_amd64_encoding nature_common_test m)
add_test(test_amd64_encoding test_amd64_encoding)

add_executable(test_riscv64_encoding test_riscv64_encoding.c)
target_link_libraries(test_riscv64_encoding nature_common_test m)
add_test(test_riscv64_encoding test_riscv64_encoding)

add_executable(test_bitmap test_bitmap.c)
target_link_libraries(test_bitmap nature_common_test m)
add_test(test_bitmap test_bitmap)

add_executable(test_scanner_numbers test_scanner_numbers.c)
target_link_libraries(test_scanner_numbers nature_common_test m)
add_test(test_scanner_numbers test_scanner_numbers)

add_executable(test_ld
    test_ld.c
    test_ld_elf_common.c
    test_ld_elf_unit.c
    test_ld_elf_input.c
    test_ld_elf_archive.c
    test_ld_elf_script.c
    test_ld_elf_crt.c
    test_ld_elf_basic.c
    test_ld_elf_layout.c
    test_ld_elf_linker_defined.c
    test_ld_elf_link_order.c
    test_ld_elf_reloc.c
    test_ld_elf_reloc_aarch64.c
    test_ld_elf_reloc_x86_64.c
    test_ld_elf_reloc_x86_64_tls.c
    test_ld_elf_reloc_riscv64.c
    test_ld_elf_aarch64_got.c
    test_ld_elf_riscv_uleb.c
    test_ld_elf_rel_input.c
    test_ld_elf_relax.c
    test_ld_elf_unwind.c
    test_ld_elf_ifunc.c
    test_ld_elf_property.c
    test_ld_elf_property_notes.c
    test_ld_elf_relro.c
    test_ld_elf_debug.c
    test_ld_elf_debug_link.c
    test_ld_elf_debug_merge.c
    test_ld_elf_dynamic.c
    test_ld_elf_pie.c
    test_ld_elf_zlib.c
    test_ld_macho_common.c
    test_ld_macho_unit.c
    test_ld_macho_input.c
    test_ld_macho_dylib.c
    test_ld_macho_binary_dylib.c
    test_ld_macho_platform.c
    test_ld_macho_symbols.c
    test_ld_macho_resolver.c
    test_ld_macho_reloc.c
    test_ld_macho_codegen.c
    test_ld_macho_unwind.c
    test_ld_macho_synthetic.c
    test_ld_macho.c
    test_ld_elf.c)
target_link_libraries(test_ld nature_common_test m)
add_test(test_ld test_ld)

add_executable(test_ld_coff_basic test_ld_coff_basic.c)
target_link_libraries(test_ld_coff_basic nature_common_test m)
add_test(test_ld_coff_basic test_ld_coff_basic)

add_executable(test_ld_coff_failifmismatch test_ld_coff_failifmismatch.c)
target_link_libraries(test_ld_coff_failifmismatch nature_common_test m)
add_test(test_ld_coff_failifmismatch test_ld_coff_failifmismatch)

add_executable(test_ld_coff_input_hardening test_ld_coff_input_hardening.c)
target_link_libraries(test_ld_coff_input_hardening nature_common_test m)
add_test(test_ld_coff_input_hardening test_ld_coff_input_hardening)

add_executable(test_ld_coff_comdat test_ld_coff_comdat.c)
target_link_libraries(test_ld_coff_comdat nature_common_test m)
add_test(test_ld_coff_comdat test_ld_coff_comdat)

add_executable(test_ld_coff_import_directories
        test_ld_coff_import_directories.c)
target_link_libraries(test_ld_coff_import_directories nature_common_test m)
add_test(test_ld_coff_import_directories test_ld_coff_import_directories)

add_executable(test_ld_coff_nodefaultlib test_ld_coff_nodefaultlib.c)
target_link_libraries(test_ld_coff_nodefaultlib nature_common_test m)
add_test(test_ld_coff_nodefaultlib test_ld_coff_nodefaultlib)

add_executable(test_ld_coff_sysroot test_ld_coff_sysroot.c)
target_compile_definitions(test_ld_coff_sysroot PRIVATE
        NATURE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_ld_coff_sysroot nature_common_test m)
add_test(test_ld_coff_sysroot test_ld_coff_sysroot)

add_executable(test_ld_coff_crt test_ld_coff_crt.c)
target_compile_definitions(test_ld_coff_crt PRIVATE
        NATURE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_ld_coff_crt nature_common_test m)
add_test(test_ld_coff_crt test_ld_coff_crt)

add_executable(test_ld_coff_capability ../src/ld/ld_coff_capability_test.c)
target_compile_definitions(test_ld_coff_capability PRIVATE
        LD_COFF_CAPABILITY_TEST_MAIN)
target_link_libraries(test_ld_coff_capability nature_common_test m)
add_test(test_ld_coff_capability test_ld_coff_capability)

add_executable(test_coff_capability_path test_coff_capability_path.c)
target_link_libraries(test_coff_capability_path nature_common_test m)
add_test(test_coff_capability_path test_coff_capability_path)

add_executable(test_coff_writer ../src/binary/coff/coff_writer_test.c)
target_compile_definitions(test_coff_writer PRIVATE COFF_WRITER_TEST_MAIN)
target_link_libraries(test_coff_writer nature_common_test m)
add_test(test_coff_writer test_coff_writer)

add_executable(test_coff_amd64 ../src/binary/coff/coff_amd64_test.c)
target_compile_definitions(test_coff_amd64 PRIVATE COFF_AMD64_TEST_MAIN)
target_link_libraries(test_coff_amd64 nature_common_test m)
add_test(test_coff_amd64 test_coff_amd64)

add_executable(test_coff_amd64_tls_layout test_coff_amd64_tls_layout.c)
target_compile_definitions(test_coff_amd64_tls_layout PRIVATE
        NATURE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_coff_amd64_tls_layout nature_common_test m)
add_test(test_coff_amd64_tls_layout test_coff_amd64_tls_layout)

add_executable(test_windows_linker_helpers test_windows_linker_helpers.c)
target_link_libraries(test_windows_linker_helpers nature_common_test m)
add_test(test_windows_linker_helpers test_windows_linker_helpers)

add_executable(test_archive_writer test_archive_writer.c)
target_link_libraries(test_archive_writer nature_common_test m)
add_test(test_archive_writer test_archive_writer)

if (WIN32)
    set(WINDOWS_C_ABI_FIXTURE_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/windows_c_nature_abi")
    file(MAKE_DIRECTORY "${WINDOWS_C_ABI_FIXTURE_DIR}")
    configure_file(windows/abi_fixture/main.n
            "${WINDOWS_C_ABI_FIXTURE_DIR}/main.n" COPYONLY)
    configure_file(windows/abi_fixture/package.toml
            "${WINDOWS_C_ABI_FIXTURE_DIR}/package.toml" COPYONLY)
    set(WINDOWS_C_ABI_OBJECT
            "${WINDOWS_C_ABI_FIXTURE_DIR}/abi_fixture.obj")
    add_custom_command(
            OUTPUT "${WINDOWS_C_ABI_OBJECT}"
            COMMAND "${CMAKE_C_COMPILER}" -std=gnu11 -O2 -fno-lto
                    -fno-sanitize=all -funwind-tables -c
                    "${CMAKE_CURRENT_SOURCE_DIR}/windows/abi_fixture/abi_fixture.c"
                    -o "${WINDOWS_C_ABI_OBJECT}"
            DEPENDS windows/abi_fixture/abi_fixture.c
            VERBATIM)
    add_custom_target(windows_c_nature_abi_fixture ALL
            DEPENDS "${WINDOWS_C_ABI_OBJECT}")
    add_test(NAME test_windows_c_nature_abi
            COMMAND "${CMAKE_COMMAND}"
                    "-DNATURE_BIN=$<TARGET_FILE:nature>"
                    "-DNATURE_ROOT=${PROJECT_SOURCE_DIR}"
                    "-DFIXTURE_DIR=${WINDOWS_C_ABI_FIXTURE_DIR}"
                    "-DOUTPUT_EXE=${WINDOWS_C_ABI_FIXTURE_DIR}/abi.exe"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/windows/run_c_nature_abi.cmake")
    set_property(TEST test_windows_c_nature_abi PROPERTY TIMEOUT 120)
endif ()

add_executable(test_ld_tapi test_ld_tapi.c)
target_compile_definitions(test_ld_tapi PRIVATE TEST_LD_TAPI_STANDALONE)
target_link_libraries(test_ld_tapi nature_common_test m)
add_test(test_ld_tapi test_ld_tapi)

# ---------------------------------- cases 测试 ------------------------------------------
set(TEST_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/tmp)
file(MAKE_DIRECTORY ${TEST_OUTPUT_DIR})
message("[test cases]make temp dir ${TEST_OUTPUT_DIR} success")

# 读取 features 目录下的所有 .c 文件
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/features/*.c)

set(WINDOWS_FEATURE_SKIP "")
if (WIN32)
    get_filename_component(WINDOWS_HOST_TOOLCHAIN_BIN
            "${CMAKE_C_COMPILER}" DIRECTORY)
    set(WINDOWS_FEATURE_POLICY
            "${CMAKE_CURRENT_SOURCE_DIR}/windows/feature-eligibility-policy.tsv")
    file(STRINGS "${WINDOWS_FEATURE_POLICY}" WINDOWS_FEATURE_POLICY_LINES)
    foreach (POLICY_LINE IN LISTS WINDOWS_FEATURE_POLICY_LINES)
        if (POLICY_LINE STREQUAL "" OR POLICY_LINE MATCHES "^#")
            continue()
        endif ()
        string(REPLACE "\t" ";" POLICY_FIELDS "${POLICY_LINE}")
        list(LENGTH POLICY_FIELDS POLICY_FIELD_COUNT)
        if (NOT POLICY_FIELD_COUNT EQUAL 3)
            message(FATAL_ERROR
                    "invalid Windows feature policy line: ${POLICY_LINE}")
        endif ()
        list(GET POLICY_FIELDS 0 POLICY_TEST_NAME)
        list(GET POLICY_FIELDS 1 POLICY_STATE)
        if (POLICY_STATE STREQUAL "skip")
            list(APPEND WINDOWS_FEATURE_SKIP "${POLICY_TEST_NAME}")
        endif ()
    endforeach ()
endif ()

foreach (TEST_SOURCE ${TEST_SOURCES})
    # 获取不带扩展名的文件名作为测试标识
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    message("[test cases] find test source ${TEST_SOURCE}")

    if (WIN32 AND TEST_NAME IN_LIST WINDOWS_FEATURE_SKIP)
        message("[test cases] skip Windows-ineligible test ${TEST_NAME}")
        continue()
    endif ()

    # 生成测试可执行文件 - 只链接静态库，不重复编译源文件
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} nature_common_test m pthread) # 链接数学计算库

    # 查找对应的测试用例文件或目录
    set(CASE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/features/cases/${TEST_NAME}")
    set(TESTAR_FILE "")

    # 判断是文件夹还是文件来设置工作目录和入口文件
    # testar 和 n 文件可以同时存在，在 main 中选择自己需要的测试方式
    # A stale/empty directory with the same name as a single-file fixture
    # must not shadow <test-name>.n.  Treat a case as directory-backed only
    # when its actual entry point exists.
    set(PLATFORM_ENTRY_FILE
            "main.${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}.n")
    if (EXISTS "${CASE_PATH}/${PLATFORM_ENTRY_FILE}")
        set(WORK_DIR ${CASE_PATH})
        set(TESTAR_FILE "${CMAKE_CURRENT_SOURCE_DIR}/features/cases/${TEST_NAME}.testar")
        set(ENTRY_FILE "${PLATFORM_ENTRY_FILE}")
    elseif (EXISTS "${CASE_PATH}/main.n")
        set(WORK_DIR ${CASE_PATH})
        set(TESTAR_FILE "${CMAKE_CURRENT_SOURCE_DIR}/features/cases/${TEST_NAME}.testar")
        set(ENTRY_FILE main.n)
    elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/features/cases/${TEST_NAME}.testar")
        set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/features/cases)
        set(TESTAR_FILE "${TEST_NAME}.testar")
        set(ENTRY_FILE "${TEST_NAME}.n")
        target_compile_definitions(${TEST_NAME} PRIVATE TEST_MODE)
    else ()
        set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/features/cases)
        set(ENTRY_FILE "${TEST_NAME}.n")
    endif ()

    # 添加测试
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} WORKING_DIRECTORY ${WORK_DIR})

    if (TEST_NAME STREQUAL "20250802_02_http")
        # This fixture runs 2,100 concurrent upload requests in its stress case.
        set_property(TEST ${TEST_NAME} PROPERTY TIMEOUT 300)
    else ()
        set_property(TEST ${TEST_NAME} PROPERTY TIMEOUT 120)
    endif ()

    set(FEATURE_TEST_ENVIRONMENT
            "BUILD_OS=$ENV{BUILD_OS}"
            "BUILD_ARCH=$ENV{BUILD_ARCH}"
            "NPKG_PATH=${PACKAGE_BINARY_PATH}"
            "BUILD_OUTPUT_DIR=${TEST_OUTPUT_DIR}"
            "ENTRY_FILE=${ENTRY_FILE}"
            "TESTAR_FILE=${TESTAR_FILE}"
            "NATURE_ROOT=${PROJECT_SOURCE_DIR}")
    if (WIN32)
        # Keep the inherited Windows PATH intact and ensure MinGW host runtime
        # DLLs used by the C test harness can be found by the Windows loader.
        set_property(TEST ${TEST_NAME} PROPERTY ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${WINDOWS_HOST_TOOLCHAIN_BIN}")
    else ()
        list(APPEND FEATURE_TEST_ENVIRONMENT
                "PATH=/usr/local/nature/bin:$ENV{PATH}")
    endif ()
    set_property(TEST ${TEST_NAME} PROPERTY ENVIRONMENT
            "${FEATURE_TEST_ENVIRONMENT}")

    message("[test cases] register test ${TEST_NAME} success, work_dir=${WORK_DIR}, entry_file=${ENTRY_FILE}, testar_file=${TESTAR_FILE}")
endforeach ()

# ---------------------------------- 通过环境变量启用 runtime main debug 测试 ------------------------------------------
if (DEFINED ENV{RUNTIME_DEBUG_CASE})
    message("RUNTIME_DEBUG_CASE is defined: $ENV{RUNTIME_DEBUG_CASE}")

    if (CMAKE_SYSTEM_NAME STREQUAL "linux" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(LIBUV_STATIC "${CMAKE_SOURCE_DIR}/lib/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}/libuv_gcc.a")
    else ()
        set(LIBUV_STATIC "${CMAKE_SOURCE_DIR}/lib/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}/libuv.a")
    endif ()
    message("LIBUV_STATIC: ${LIBUV_STATIC}")

    set(TEST_IDENT $ENV{RUNTIME_DEBUG_CASE})
    message("[test_runtime_debug] find RUNTIME_DEBUG_CASE: ${TEST_IDENT}")

    if (IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/features/cases/${TEST_IDENT})
        set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/features/cases/${TEST_IDENT})
        set(ENTRY main.n)
    else ()
        set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/features/cases)
        set(ENTRY "${TEST_IDENT}.n")
    endif ()

    message("[test_runtime_debug] work_dir: ${WORK_DIR}, entry: ${ENTRY}")

    set(LIBMAIN_STATIC ${TEST_OUTPUT_DIR}/libmain.a)
    add_custom_target(generate_libmain ALL
            COMMAND ${CMAKE_COMMAND} -E remove -f ${LIBMAIN_STATIC}
            COMMAND env BUILD_OUTPUT_DIR=${TEST_OUTPUT_DIR} NATURE_ROOT=${PROJECT_SOURCE_DIR} $<TARGET_FILE:nature> build --archive ${ENTRY}
            WORKING_DIRECTORY ${WORK_DIR}
            DEPENDS nature
            BYPRODUCTS ${LIBMAIN_STATIC}
            COMMENT "force rebuilding ${LIBMAIN_STATIC}"
    )

    message("[test_runtime_debug] LIBMAIN_STATIC: ${LIBMAIN_STATIC}")

    # 2. target_link_libraries 生成最终可执行文件, 依赖上一部生成的 libmain.a
    add_executable(test_runtime_debug test_runtime_debug.c ${RUNTIMES} ${UTILS})


    #    if (CMAKE_SYSTEM_NAME MATCHES "linux")
    #        set(CMAKE_EXE_LINKER_FLAGS "-static")
    #        set_target_properties(test_runtime_debug PROPERTIES LINK_FLAGS "-static")
    #    endif ()
    #    target_compile_options(test_runtime_debug PRIVATE -O3)
    #    target_compile_definitions(test_runtime_debug PRIVATE NDEBUG)

    add_dependencies(test_runtime_debug generate_libmain)
    #    target_compile_definitions(test_runtime_debug PRIVATE TEST_MODE=1)
    target_link_libraries(test_runtime_debug ${LIBMAIN_STATIC} ${LIBUV_STATIC} ${LIBC_STATIC})
    add_test(NAME test_runtime_debug COMMAND test_runtime_debug WORKING_DIRECTORY ${WORK_DIR})

    message("[test_runtime_debug] register test_runtime_debug case ${TEST_IDENT} success, work_dir=${WORK_DIR}, entry_file=${ENTRY}")
endif ()

add_executable(test_amd64_windows_abi test_amd64_windows_abi.c)
target_link_libraries(test_amd64_windows_abi nature_common_test m)
add_test(test_amd64_windows_abi test_amd64_windows_abi)
