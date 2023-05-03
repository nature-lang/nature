cmake_minimum_required(VERSION 3.22)
project(nature C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE OFF)

file(READ "VERSION" PROJECT_VERSION)

# 打印当前的项目版本
message("nature version: ${PROJECT_VERSION}")
message("workdir: ${CMAKE_CURRENT_BINARY_DIR}")

# 设置交叉编译工具链名称
if (CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(CMAKE_SYSTEM_NAME darwin)
endif ()
if (CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(CMAKE_SYSTEM_NAME linux)
endif ()
if (CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
    set(CMAKE_SYSTEM_PROCESSOR amd64)
endif ()

# 设置 release 模式下的优化等级
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
endif ()
# 设置 debug 模式下编译输出
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message("DEBUGF enabled")
    add_compile_definitions(DEBUG)
    add_compile_definitions(DEBUG_LIR)
    add_compile_definitions(DEBUG_ASM)
    add_compile_definitions(DEBUG_PARSER)
    add_compile_definitions(DEBUG_INTERVAL)
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O2")
endif ()

aux_source_directory(${PROJECT_SOURCE_DIR}/cmd CMD)
file(GLOB_RECURSE SRC ${PROJECT_SOURCE_DIR}/src/*.c)
aux_source_directory(${PROJECT_SOURCE_DIR}/utils UTILS)

# include 搜索路径, 有了这个就不需要在 add_executable 中声明 .h 文件了
include_directories(.)

add_executable(nature main.c ${SRC} ${CMD} ${UTILS})
target_link_libraries(nature m) # 链接数学计算库

enable_testing()
add_subdirectory(tests)
add_subdirectory(runtime EXCLUDE_FROM_ALL)

#--------------------------------- install ---------------------------------
install(TARGETS nature DESTINATION nature/bin)
install(DIRECTORY lib/ DESTINATION nature/lib)
install(DIRECTORY std/ DESTINATION nature/std)
install(FILES LICENSE DESTINATION nature/)
install(FILES VERSION DESTINATION nature/)


include(cmake/CMackCPack.cmake)
