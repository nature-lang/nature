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

# ---------------------------------- cases 测试 ------------------------------------------
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tmp)
message("[test cases]make temp dir ${CMAKE_CURRENT_SOURCE_DIR}/tmp success")

# 读取 features 目录下的所有 .c 文件
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/features/*.c)
foreach (TEST_SOURCE ${TEST_SOURCES})
    # 获取不带扩展名的文件名作为测试标识
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    message("[test cases] find test source ${TEST_SOURCE}")

    # 生成测试可执行文件 - 只链接静态库，不重复编译源文件
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} nature_common_test m pthread) # 链接数学计算库

    # 查找对应的测试用例文件或目录
    set(CASE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/features/cases/${TEST_NAME}")

    # 判断是文件夹还是文件来设置工作目录和入口文件
    # testar 和 n 文件可以同时存在，在 main 中选择自己需要的测试方式
    if (IS_DIRECTORY ${CASE_PATH})
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

    set_property(TEST ${TEST_NAME} PROPERTY TIMEOUT 120)
    set_property(TEST ${TEST_NAME}
            PROPERTY
            ENVIRONMENT "BUILD_OS=$ENV{BUILD_OS};BUILD_ARCH=$ENV{BUILD_ARCH};NPKG_PATH=${PACKAGE_BINARY_PATH};BUILD_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tmp;ENTRY_FILE=${ENTRY_FILE};TESTAR_FILE=${TESTAR_FILE};NATURE_ROOT=${PROJECT_SOURCE_DIR};PATH=/usr/local/nature/bin:$ENV{PATH};")

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

    set(LIBMAIN_STATIC ${CMAKE_CURRENT_SOURCE_DIR}/tmp/libmain.a)
    add_custom_target(generate_libmain ALL
            COMMAND ${CMAKE_COMMAND} -E remove -f ${LIBMAIN_STATIC}
            COMMAND env BUILD_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tmp NATURE_ROOT=${PROJECT_SOURCE_DIR} $<TARGET_FILE:nature> build --archive ${ENTRY}
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
    # target_compile_definitions(test_runtime_debug PRIVATE NDEBUG)

    add_dependencies(test_runtime_debug generate_libmain)

    target_link_libraries(test_runtime_debug ${LIBMAIN_STATIC} ${LIBUV_STATIC} ${LIBC_STATIC})
    add_test(NAME test_runtime_debug COMMAND test_runtime_debug WORKING_DIRECTORY ${WORK_DIR})

    message("[test_runtime_debug] register test_runtime_debug case ${TEST_IDENT} success, work_dir=${WORK_DIR}, entry_file=${ENTRY}")
endif ()
