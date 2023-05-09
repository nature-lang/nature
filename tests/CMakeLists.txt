add_subdirectory(blackbox)

add_executable(test_hello test_hello.c ${SRC} ${CMD} ${UTILS})
add_test(test_hello test_hello)

#add_executable(test_linker test_linker.c ${SRC} ${CMD} ${UTILS})
#add_test(test_linker test_linker)

add_executable(test_bitmap test_bitmap.c ${UTILS})
add_test(test_bitmap test_bitmap)

