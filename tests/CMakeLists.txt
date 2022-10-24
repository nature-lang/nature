# Find cmocka
add_subdirectory(cmocka)

# copy stubs 文件到程序执行目录下的 stub, 但是并不会每一次都同步
#file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/stubs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_hello test_hello.c)
target_link_libraries(test_hello cmocka)
add_test(test_hello test_hello)

add_executable(test_linker test_linker.c ${UTILS} ${SRC_BINARY})
target_link_libraries(test_linker cmocka)
add_test(test_linker test_linker)

add_executable(test_build test_build.c ${BUILD_SOURCE_FILES})
target_link_libraries(test_build cmocka)
add_test(test_build test_build)

add_executable(test_ssa test_ssa.c ${BUILD_SOURCE_FILES})
target_link_libraries(test_ssa cmocka)
add_test(test_ssa test_ssa)

add_executable(test_reg_alloc test_reg_alloc.c ${BUILD_SOURCE_FILES})
target_link_libraries(test_reg_alloc cmocka)
add_test(test_reg_alloc test_reg_alloc)

# build stubs
set(STUB_FILES 20221025_01_hello 20221025_02_sum)
foreach (STUB_FILE ${STUB_FILES})
    add_executable(${STUB_FILE} ${STUB_FILE}.c ${BUILD_SOURCE_FILES})
    target_link_libraries(${STUB_FILE} cmocka)
    add_test(NAME ${STUB_FILE} COMMAND ${STUB_FILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
    set_property(TEST ${STUB_FILE} PROPERTY ENVIRONMENT "FORCE_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tmp/main;ENTRY_FILE=${STUB_FILE}.n")
    message("register ${STUB_FILE} success")
endforeach ()

# build stub dir
