# 创建测试临时目录
message("will make dir ${CMAKE_CURRENT_SOURCE_DIR}/tmp")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tmp)

# build stubs
set(STUB_FILES
        20221025_01_hello
        20221025_02_sum
        20221025_03_call
        20221025_04_if
        20221025_05_while
        20221025_06_closure
        20221025_07_array
        20221113_01_rest)

foreach (STUB_FILE ${STUB_FILES})
    add_executable(${STUB_FILE} ${STUB_FILE}.c ${BUILD_SOURCE_FILES})
    target_link_libraries(${STUB_FILE} cmocka-static)
    # 如果 ${CMAKE_CURRENT_SOURCE_DIR}/stubs + STUB_FILE 是一个文件夹, 那工作目录应该是 ${CMAKE_CURRENT_SOURCE_DIR}/stubs + STUB_FILE
    set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
    set(ENTRY_FILE ${STUB_FILE}.n)
    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/stubs/${STUB_FILE})
        set(WORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stubs/${STUB_FILE})
        set(ENTRY_FILE main.n)
    endif ()
    message(DEBUG "work_dir=${WORK_DIR}, entry_file=${ENTRY_FILE}")


    add_test(NAME ${STUB_FILE} COMMAND ${STUB_FILE}
            WORKING_DIRECTORY ${WORK_DIR})

    set_property(TEST ${STUB_FILE}
            PROPERTY
            ENVIRONMENT "BUILD_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tmp;ENTRY_FILE=${ENTRY_FILE};NATURE_ROOT=${PROJECT_SOURCE_DIR};")
    message("register ${STUB_FILE}, success")
endforeach ()

# build stub dir
