set(LIB_OUTPUT_PREFIX ${PROJECT_SOURCE_DIR}/debug/lib)

# linux x86_64
set(CMAKE_C_COMPILER x86_64-linux-gnu-gcc)
add_library(runtime_linux_amd64 STATIC ${UTILS} ${RUNTIME_FILES})
set_target_properties(runtime_linux_amd64 PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${LIB_OUTPUT_PREFIX}/linux_amd64
        OUTPUT_NAME runtime
        CMAKE_C_COMPILER x86_64-linux-gnu-gcc)

# linux riscv64
set(CMAKE_C_COMPILER riscv64-linux-gnu-gcc)
add_library(runtime_linux_riscv64 STATIC ${UTILS} ${RUNTIME_FILES})
set_target_properties(runtime_linux_riscv64 PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${LIB_OUTPUT_PREFIX}/linux_riscv64
        OUTPUT_NAME runtime
        CMAKE_C_COMPILER riscv64-linux-gnu-gcc)
