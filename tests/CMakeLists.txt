# Find cmocka
add_subdirectory(cmocka)


# copy stubs 文件到程序执行目录下的 stub
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/stubs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_hello test_hello.c)
target_link_libraries(test_hello cmocka)
add_test(test_hello test_hello)

add_executable(test_linker test_linker.c ${UTILS} ${SRC_BINARY})
target_link_libraries(test_linker cmocka)
add_test(test_linker test_linker)
