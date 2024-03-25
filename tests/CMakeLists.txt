add_subdirectory(blackbox)

add_executable(test_hello test_hello.c ${SRC} ${CMD} ${UTILS})
add_test(test_hello test_hello)

add_executable(test_bitmap test_bitmap.c ${UTILS})
add_test(test_bitmap test_bitmap)

add_executable(test_slice test_slice.c ${UTILS})
add_test(test_slice test_slice)

# runtime test
ENABLE_LANGUAGE(ASM)
FILE(GLOB_RECURSE RUNTIMES ../runtime/*c ../runtime/aco/acosw.S)

set(LIBUV_STATIC "${CMAKE_SOURCE_DIR}/lib/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}/libuv.a")
message(STATUS "LIBUV_STATIC: ${LIBUV_STATIC}")

add_executable(test_page_alloc test_page_alloc.c ${RUNTIMES} ${UTILS})
target_link_libraries(test_page_alloc ${LIBUV_STATIC})
add_test(test_page_alloc test_page_alloc)

add_executable(test_rt_mutex test_rt_mutex.c ${RUNTIMES} ${UTILS})
target_link_libraries(test_rt_mutex ${LIBUV_STATIC})
add_test(test_rt_mutex test_rt_mutex)

# build cases
# 创建测试临时目录
message("will make dir ${CMAKE_CURRENT_SOURCE_DIR}/tmp")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tmp)
aux_source_directory(. TEST_SOURCES)

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


# 多阶段构建测试
set(TEST_NAME "20221025_01_hello.n")
set(CASES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cases)

if (IS_DIRECTORY ${CASES_DIR}/${TEST_NAME})
    set(WORK_DIR ${CASES_DIR}/${TEST_NAME})
    set(ENTRY main.n)
else ()
    set(WORK_DIR ${CASES_DIR})
    set(ENTRY ${TEST_NAME})
endif ()
add_test(NAME ${TEST_NAME} COMMAND nature build --archive ${ENTRY} WORKING_DIRECTORY ${WORK_DIR})
set_property(TEST ${TEST_NAME}
        PROPERTY
        ENVIRONMENT "NPKG_PATH=${PACKAGE_BINARY_PATH};BUILD_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tmp;ENTRY_FILE=${ENTRY_FILE};NATURE_ROOT=${PROJECT_SOURCE_DIR};PATH=/usr/local/nature/bin:$ENV{PATH};")


message(STATUS "register test ${TEST_NAME} file success, cases work_dir=${WORK_DIR}, entry_file=${ENTRY}")


# 2. target_link_libraries 生成测试