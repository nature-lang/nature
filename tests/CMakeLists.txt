# Find cmocka
add_subdirectory(cmocka)

add_executable(test_hello test_hello.c)
target_link_libraries(test_hello cmocka)
add_test(test_hello test_hello)

# build test nature root

# LIST test files