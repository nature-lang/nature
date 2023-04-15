# Find cmocka-static
set(BUILD_STATIC_LIB true)
# We want to link to cmocka-static, so we need to set this option before calling the FetchContent_MakeAvailable
# We also don't care about example and tests
set(WITH_STATIC_LIB ON CACHE BOOL "CMocka: Build with a static library" FORCE)
set(WITH_CMOCKERY_SUPPORT OFF CACHE BOOL "CMocka: Install a cmockery header" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "CMocka: Build examples" FORCE)
set(UNIT_TESTING OFF CACHE BOOL "CMocka: Build with unit testing" FORCE)
set(PICKY_DEVELOPER OFF CACHE BOOL "CMocka: Build with picky developer flags" FORCE)

add_subdirectory(cmocka)
add_subdirectory(blackbox)

# copy stubs 文件到程序执行目录下的 stub, 但是并不会每一次都同步
#file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/stubs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_hello test_hello.c ${BUILD_SOURCE_FILES})
target_link_libraries(test_hello cmocka-static)
add_test(test_hello test_hello)

add_executable(test_linker test_linker.c ${UTILS} ${SRC_BINARY})
target_link_libraries(test_linker cmocka-static)
add_test(test_linker test_linker)

add_executable(test_build test_build.c ${BUILD_SOURCE_FILES})
target_link_libraries(test_build cmocka-static)
add_test(test_build test_build)

add_executable(test_bitmap test_bitmap.c ${UTILS})
target_link_libraries(test_bitmap cmocka-static)
add_test(test_bitmap test_bitmap)

# 如果是 linux-amd64 下的编译，则去 project 目录下查找
if (CMAKE_SYSTEM_NAME STREQUAL linux AND CMAKE_SYSTEM_PROCESSOR STREQUAL amd64)
    set(UCONTEXT_LIB "${PROJECT_SOURCE_DIR}/lib/linux_amd64/libucontext.a")
    message("current is linux-amd64, will use UCONTEXT_LIB=${UCONTEXT_LIB}")
    add_executable(test_gc test_gc.c ${RUNTIME_FILES} ${BUILD_SOURCE_FILES})
    target_link_libraries(test_gc cmocka-static ${UCONTEXT_LIB})
    add_test(test_gc test_gc)
    set_property(TEST test_gc PROPERTY ENVIRONMENT
            "WORK_DIR=${PROJECT_SOURCE_DIR}/tests/stubs/test_gc;")
endif ()
