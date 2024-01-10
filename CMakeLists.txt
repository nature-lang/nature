cmake_minimum_required(VERSION 3.22)
project(nature C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE OFF)

include(cmake/cross.cmake)

file(READ "VERSION" PROJECT_VERSION)

# 打印当前的项目版本
message("nature version: ${PROJECT_VERSION}")
message("cmake workdir: ${CMAKE_CURRENT_BINARY_DIR}")

string(TIMESTAMP BUILD_TIME "%Y-%m-%d")
message("build time: ${BUILD_TIME}")

# 设置 release 模式下的优化等级
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(BUILD_TYPE "release")
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
    set(BUILD_TYPE "release")
endif ()
# 设置 debug 模式下编译输出
if (CMAKE_BUILD_TYPE MATCHES Debug)
    set(BUILD_TYPE "debug")
    add_compile_definitions(DEBUG)
    #    add_compile_definitions(DEBUG_LIR)
    #    add_compile_definitions(DEBUG_ASM)
    add_compile_definitions(DEBUG_PARSER)
    #    add_compile_definitions(DEBUG_INTERVAL)
    add_compile_definitions(ASSERT_ERROR)
    #    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O2")
    message("cmake type is debug")
endif ()

configure_file("${PROJECT_SOURCE_DIR}/config/config.h.in" "${PROJECT_SOURCE_DIR}/config/config.h")

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

# custom golang build package
set(PACKAGE_BINARY_NAME npkg)
set(GOBIN /usr/local/go/bin/go)

add_custom_target(${PACKAGE_BINARY_NAME} ALL
        COMMAND GOOS=${CMAKE_SYSTEM_NAME} GOARCH=${CMAKE_SYSTEM_PROCESSOR} ${GOBIN} build -o ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_BINARY_NAME} ${PROJECT_SOURCE_DIR}/package/main.go
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/package
        COMMENT "package manager npkg"
)

#--------------------------------- install ---------------------------------
install(TARGETS nature DESTINATION nature/bin)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_BINARY_NAME} DESTINATION nature/bin)
install(DIRECTORY lib/ DESTINATION nature/lib)
install(DIRECTORY std/ DESTINATION nature/std)
install(FILES LICENSE DESTINATION nature/)
install(FILES VERSION DESTINATION nature/)


include(cmake/cpack.cmake)

