add_subdirectory(blackbox)

# copy cases 文件到程序执行目录下的 stub, 但是并不会每一次都同步
#file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/cases DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_hello test_hello.c ${SRC} ${CMD} ${UTILS})
add_test(test_hello test_hello)

add_executable(test_linker test_linker.c ${SRC} ${CMD} ${UTILS})
add_test(test_linker test_linker)

add_executable(test_build test_build.c ${SRC} ${CMD} ${UTILS})
add_test(test_build test_build)

add_executable(test_bitmap test_bitmap.c ${UTILS})
add_test(test_bitmap test_bitmap)

