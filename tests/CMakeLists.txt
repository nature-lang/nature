include(CTest)

# runtime test
ENABLE_LANGUAGE(ASM)
FILE(GLOB_RECURSE RUNTIMES ../runtime/*c ../runtime/aco/acosw.S)

set(LIBUV_STATIC "${CMAKE_SOURCE_DIR}/lib/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}/libuv.a")
message("LIBUV_STATIC: ${LIBUV_STATIC}")

set(LIBUC_STATIC "${CMAKE_SOURCE_DIR}/lib/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}/libc.a")
message("LIBUC_STATIC: ${LIBUC_STATIC}")

add_executable(test_page_alloc test_page_alloc.c ${RUNTIMES} ${UTILS})
target_link_libraries(test_page_alloc ${LIBUV_STATIC})
add_test(test_page_alloc test_page_alloc)

add_executable(test_rt_mutex test_rt_mutex.c ${RUNTIMES} ${UTILS})
target_link_libraries(test_rt_mutex ${LIBUV_STATIC})
add_test(test_rt_mutex test_rt_mutex)

add_executable(test_runtime_fn test_runtime_fn.c ${RUNTIMES} ${UTILS})
target_link_libraries(test_runtime_fn ${LIBUV_STATIC})
add_test(test_runtime_fn test_runtime_fn)

add_executable(test_yaml test_yaml.c ${UTILS})
add_test(test_yaml test_yaml)
set_property(TEST test_yaml PROPERTY ENVIRONMENT "YAML_PATH=${CMAKE_CURRENT_SOURCE_DIR}/assets/travis.yaml;")

add_executable(test_arm64_encoding test_arm64_encoding.c ${UTILS} ${SRC})
add_test(test_arm64_encoding test_arm64_encoding)

# ---------------------------------- cases 测试 ------------------------------------------
# cases test, 获取 cases 一级目录下的所有文件，排除 mock
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tmp)
message("[test cases]make temp dir ${CMAKE_CURRENT_SOURCE_DIR}/tmp success")

# 读取 ${CMAKE_CURRENT_SOURCE_DIR}/cases 一级目录中的所有文件或者文件夹
file(GLOB TEST_CASES ${CMAKE_CURRENT_SOURCE_DIR}/cases/*)
foreach (TEST_CASE ${TEST_CASES})
    message("[test cases] find test case ${TEST_CASE}")

    # 生成 test_ident, 如果是目录则取最后一级目录，如果是文件，则取最后一次文件，去掉 .n 后缀
    # 文件示例 /tmp/tmp.wKB2qsXqAh/tests/cases/20230913_00_env_struct.n
    # 目录示例 /tmp/tmp.wKB2qsXqAh/tests/cases/20240321_01_coroutine_power
    get_filename_component(TEST_NAME ${TEST_CASE} NAME_WE)

    # 直接生成测试用例可执行文件，由测试用例进行编译和输出测试
    add_executable(${TEST_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_NAME}.c ${SRC} ${CMD} ${UTILS})

    # 判断 TEST_CASE 是文件夹还是目录，从而生成合适的 WORK_DIR 和 ENTRY_FILE
    if (IS_DIRECTORY ${TEST_CASE})
        set(WORK_DIR ${TEST_CASE})
        set(ENTRY_FILE main.n)
    else ()
        set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cases)
        set(ENTRY_FILE ${TEST_NAME}.n)
    endif ()

    # 生成测试用例
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} WORKING_DIRECTORY ${WORK_DIR})
    set_property(TEST ${TEST_NAME}
            PROPERTY
            ENVIRONMENT "BUILD_OS=$ENV{BUILD_OS};BUILD_ARCH=$ENV{BUILD_ARCH};NPKG_PATH=${PACKAGE_BINARY_PATH};BUILD_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tmp;ENTRY_FILE=${ENTRY_FILE};NATURE_ROOT=${PROJECT_SOURCE_DIR};PATH=/usr/local/nature/bin:$ENV{PATH};")

    message("[test cases] register test ${TEST_NAME} file success, cases work_dir=${WORK_DIR}, entry_file=${ENTRY_FILE}")
endforeach ()


# ---------------------------------- 通过环境变量启用 runtime main debug 测试 ------------------------------------------
if (DEFINED ENV{RUNTIME_DEBUG_CASE})
    message("RUNTIME_DEBUG_CASE is defined: $ENV{RUNTIME_DEBUG_CASE}")

    set(TEST_IDENT $ENV{RUNTIME_DEBUG_CASE})
    message("[test_runtime_debug] find RUNTIME_DEBUG_CASE: ${TEST_IDENT}")

    if (IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cases/${TEST_IDENT})
        set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cases/${TEST_IDENT})
        set(ENTRY main.n)
    else ()
        set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cases)
        set(ENTRY "${TEST_IDENT}.n")
    endif ()

    message("[test_runtime_debug] work_dir: ${WORK_DIR}, entry: ${ENTRY}")

    set(LIBMAIN_STATIC ${CMAKE_CURRENT_SOURCE_DIR}/tmp/libmain.a)
    add_custom_command(
            OUTPUT ${LIBMAIN_STATIC}
            COMMAND env BUILD_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tmp NATURE_ROOT=${PROJECT_SOURCE_DIR} $<TARGET_FILE:nature> build --archive ${ENTRY}
            WORKING_DIRECTORY ${WORK_DIR}
            DEPENDS nature
            COMMENT "Generating ${LIBMAIN_STATIC}"
    )

    add_custom_target(generate_libmain ALL
            DEPENDS ${LIBMAIN_STATIC}
            COMMENT "Trigger libmain.a generation"
    )

    message("[test_runtime_debug] LIBMAIN_STATIC: ${LIBMAIN_STATIC}")

    # 2. target_link_libraries 生成最终可执行文件, 依赖上一部生成的 libmain.a
    add_executable(test_runtime_debug test_runtime_debug.c ${RUNTIMES} ${UTILS})
#    if (CMAKE_SYSTEM_NAME MATCHES "linux")
        #        set(CMAKE_EXE_LINKER_FLAGS "-static")
#        set_target_properties(test_runtime_debug PROPERTIES LINK_FLAGS "-static")
#    endif ()

#    add_dependencies(test_runtime_debug generate_libmain)  # 添加这行

    target_link_libraries(test_runtime_debug ${LIBMAIN_STATIC} ${LIBUV_STATIC})
    add_test(NAME test_runtime_debug COMMAND test_runtime_debug WORKING_DIRECTORY ${WORK_DIR})

    message("[test_runtime_debug] register test_runtime_debug case ${TEST_IDENT} success, work_dir=${WORK_DIR}, entry_file=${ENTRY}")
endif ()
