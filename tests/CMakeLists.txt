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
add_executable(test_page_alloc test_page_alloc.c ${RUNTIMES} ${UTILS})
target_link_libraries(test_page_alloc uv)
add_test(test_page_alloc test_page_alloc)

