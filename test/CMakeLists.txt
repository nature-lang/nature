include(FetchContent)


# Declare our target. We want the lastest stable version, not the master.
# Also specify GIT_SHALLOW to avoid cloning branch we don't care about
FetchContent_Declare(
        cmocka
        GIT_REPOSITORY https://git.cryptomilk.org/projects/cmocka.git
        GIT_TAG cmocka-1.1.5
        GIT_SHALLOW 1
)


# We want to link to cmocka-static, so we need to set this option before calling the FetchContent_MakeAvailable
# We also don't care about example and tests
set(WITH_STATIC_LIB ON CACHE BOOL "CMocka: Build with a static library" FORCE)
set(WITH_CMOCKERY_SUPPORT OFF CACHE BOOL "CMocka: Install a cmockery header" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "CMocka: Build examples" FORCE)
set(UNIT_TESTING OFF CACHE BOOL "CMocka: Build with unit testing" FORCE)
set(PICKY_DEVELOPER OFF CACHE BOOL "CMocka: Build with picky developer flags" FORCE)

# Download cmocka, and execute its cmakelists.txt
FetchContent_MakeAvailable(cmocka)

# unit test
#include_directories(${PROJECT_SOURCE_DIR}/src)


add_executable(test_amd64_mov
        test_amd64_mov.c
        ${SRC_ASSEMBLER} ${SRC_LIB})

add_executable(test_amd64_add
        test_amd64_add.c
        ${SRC_ASSEMBLER} ${SRC_LIB})

add_executable(test_amd64_sub
        test_amd64_sub.c
        ${SRC_ASSEMBLER} ${SRC_LIB})

add_executable(test_amd64_mul
        test_amd64_mul.c
        ${SRC_ASSEMBLER} ${SRC_LIB})

add_executable(test_amd64_div
        test_amd64_div.c
        ${SRC_ASSEMBLER} ${SRC_LIB})

target_link_libraries(test_amd64_mov cmocka)
target_link_libraries(test_amd64_add cmocka)
target_link_libraries(test_amd64_sub cmocka)
target_link_libraries(test_amd64_mul cmocka)
target_link_libraries(test_amd64_div cmocka)

add_test(test_amd64_mov test_amd64_mov)
add_test(test_amd64_add test_amd64_add)
add_test(test_amd64_sub test_amd64_sub)
add_test(test_amd64_mul test_amd64_mul)
add_test(test_amd64_div test_amd64_div)
