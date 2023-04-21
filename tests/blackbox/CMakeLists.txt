# 创建测试临时目录
message("will make dir ${CMAKE_CURRENT_SOURCE_DIR}/tmp")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tmp)

# build cases
file(GLOB TEST_SOURCES "*.c")


foreach (TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)

    add_executable(${TEST_NAME} ${TEST_SOURCE} ${BUILD_SOURCE_FILES})
    target_link_libraries(${TEST_NAME} cmocka-static)

    # 如果 ${CMAKE_CURRENT_SOURCE_DIR}/cases + TEST_NAME 是一个文件夹, 那工作目录应该是 ${CMAKE_CURRENT_SOURCE_DIR}/cases + TEST_NAME
    set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cases)
    set(ENTRY_FILE ${TEST_NAME}.n)

    # stub 是一个目录
    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cases/${TEST_NAME})
        set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cases/${TEST_NAME})
        set(ENTRY_FILE main.n)
    endif ()

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} WORKING_DIRECTORY ${WORK_DIR})

    set_property(TEST ${TEST_NAME}
            PROPERTY ENVIRONMENT "BUILD_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tmp;ENTRY_FILE=${ENTRY_FILE};NATURE_ROOT=${PROJECT_SOURCE_DIR};")

    message("register test ${TEST_NAME} file success, stub work_dir=${WORK_DIR}, entry_file=${ENTRY_FILE}")
endforeach ()

# build stub dir
