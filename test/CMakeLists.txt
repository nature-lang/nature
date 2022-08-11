include(FetchContent)


# Declare our target. We want the lastest stable version, not the master.
# Also specify GIT_SHALLOW to avoid cloning branch we don't care about
FetchContent_Declare(
        ../tests/cmocka
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


add_executable(test_elf test_elf.c ${SRC_ASSEMBLER} ${SRC_LIB})
target_link_libraries(test_elf cmocka)
add_test(test_elf test_elf)

add_executable(test_lower_amd64 test_lower_amd64.c ${SRC} ${SRC_ASSEMBLER} ${SRC_LIB} ${SRC_LOWER})
target_link_libraries(test_lower_amd64 ../tests/cmocka m)
add_test(test_lower_amd64 test_lower_amd64)

add_executable(test_runtime test_runtime.c ${RUNTIME_FILES})
target_link_libraries(test_runtime ../tests/cmocka)
add_test(test_runtime test_runtime)

add_executable(test_amd64_stack_machine test_amd64_stack_machine.c ${SRC} ${SRC_DEBUG} ${SRC_SEMANTIC} ${SRC_SYNTAX} ${SRC_ASSEMBLER} ${SRC_LIB} ${SRC_LOWER})
target_link_libraries(test_amd64_stack_machine ../tests/cmocka m)
add_test(test_amd64_stack_machine test_amd64_stack_machine)
