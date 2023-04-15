# Find cmocka
add_subdirectory(cmocka)
add_subdirectory(blackbox)

# copy stubs 文件到程序执行目录下的 stub, 但是并不会每一次都同步
#file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/stubs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_hello test_hello.c ${BUILD_SOURCE_FILES})
target_link_libraries(test_hello cmocka)
add_test(test_hello test_hello)

add_executable(test_linker test_linker.c ${UTILS} ${SRC_BINARY})
target_link_libraries(test_linker cmocka)
add_test(test_linker test_linker)

add_executable(test_build test_build.c ${BUILD_SOURCE_FILES})
target_link_libraries(test_build cmocka)
add_test(test_build test_build)

add_executable(test_bitmap test_bitmap.c ${UTILS})
target_link_libraries(test_bitmap cmocka)
add_test(test_bitmap test_bitmap)

add_executable(test_gc test_gc.c ${RUNTIME_FILES} ${BUILD_SOURCE_FILES})
target_link_libraries(test_gc cmocka)
add_test(test_gc test_gc)
set_property(TEST test_gc PROPERTY ENVIRONMENT
        "WORK_DIR=${PROJECT_SOURCE_DIR}/tests/stubs/test_gc;")

# build stub dir
