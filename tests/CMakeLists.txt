#add_executable(test_hello test_hello.c ${SRC} ${CMD} ${UTILS})
#add_test(test_hello test_hello)
#
#add_executable(test_bitmap test_bitmap.c ${UTILS})
#add_test(test_bitmap test_bitmap)
#
#add_executable(test_slice test_slice.c ${UTILS})
#add_test(test_slice test_slice)

# runtime test
ENABLE_LANGUAGE(ASM)
FILE(GLOB_RECURSE RUNTIMES ../runtime/*c ../runtime/aco/acosw.S)

set(LIBUV_STATIC "${CMAKE_SOURCE_DIR}/lib/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}/libuv.a")
message("LIBUV_STATIC: ${LIBUV_STATIC}")

set(LIBUC_STATIC "${CMAKE_SOURCE_DIR}/lib/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}/libc.a")
message("LIBUC_STATIC: ${LIBUC_STATIC}")


#add_executable(test_page_alloc test_page_alloc.c ${RUNTIMES} ${UTILS})
#target_link_libraries(test_page_alloc ${LIBUV_STATIC})
#add_test(test_page_alloc test_page_alloc)
#
#add_executable(test_rt_mutex test_rt_mutex.c ${RUNTIMES} ${UTILS})
#target_link_libraries(test_rt_mutex ${LIBUV_STATIC})
#add_test(test_rt_mutex test_rt_mutex)

# build cases
# 创建测试临时目录
#message("will make dir ${CMAKE_CURRENT_SOURCE_DIR}/tmp")
#file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tmp)
#aux_source_directory(. TEST_SOURCES)

# foreach (TEST_SOURCE ${TEST_SOURCES})
#     get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)

#     add_executable(${TEST_NAME} ${TEST_SOURCE} ${SRC} ${CMD} ${UTILS})

#     # 如果 ${CMAKE_CURRENT_SOURCE_DIR}/cases + TEST_NAME 是一个文件夹, 那工作目录应该是 ${CMAKE_CURRENT_SOURCE_DIR}/cases + TEST_NAME
#     set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cases)
#     set(ENTRY_FILE ${TEST_NAME}.n)

#     # cases 是一个目录
#     if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cases/${TEST_NAME})
#         set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cases/${TEST_NAME})
#         set(ENTRY_FILE main.n)
#     endif ()

#     add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} WORKING_DIRECTORY ${WORK_DIR})

#     set_property(TEST ${TEST_NAME}
#             PROPERTY
#             ENVIRONMENT "NPKG_PATH=${PACKAGE_BINARY_PATH};BUILD_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tmp;ENTRY_FILE=${ENTRY_FILE};NATURE_ROOT=${PROJECT_SOURCE_DIR};PATH=/usr/local/nature/bin:$ENV{PATH};")

#     message("register test ${TEST_NAME} file success, cases work_dir=${WORK_DIR}, entry_file=${ENTRY_FILE}")
# endforeach ()


# 多阶段构建 runtime main debug 测试
if (DEFINED ENV{RUNTIME_DEBUG_CASE})
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
    add_custom_target(build_libmain
            COMMAND env BUILD_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tmp NATURE_ROOT=${PROJECT_SOURCE_DIR} $<TARGET_FILE:nature> build --archive ${ENTRY}
            WORKING_DIRECTORY ${WORK_DIR}
            COMMENT "Generating ${LIBMAIN_STATIC}")
    add_dependencies(build_libmain nature)

    message("[test_runtime_debug] LIBMAIN_STATIC: ${LIBMAIN_STATIC}")

    # 2. target_link_libraries 生成最终可执行文件, 依赖上一部生成的 libmain.a
    add_executable(test_runtime_debug test_runtime_debug.c ${RUNTIMES} ${UTILS})
    target_link_libraries(test_runtime_debug ${LIBMAIN_STATIC} ${LIBUV_STATIC} ${LIBC_STATIC})
    add_dependencies(test_runtime_debug build_libmain)
    add_test(NAME test_runtime_debug COMMAND test_runtime_debug WORKING_DIRECTORY ${WORK_DIR})

    message("[test_runtime_debug] register test_runtime_debug case ${TEST_IDENT} success, work_dir=${WORK_DIR}, entry_file=${ENTRY}")
endif ()
