cmake_minimum_required(VERSION 3.17)
project(runtime C)

set(CMAKE_C_STANDARD 99)

include_directories(../include/musl)

set(LIBRARY_OUTPUT_PATH  ${PROJECT_SOURCE_DIR}/amd64)

add_library(runtime STATIC ${RUNTIME_FILES})