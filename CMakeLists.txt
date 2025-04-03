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
    set(CMAKE_C_FLAGS_RELEASE "-O2")
    set(BUILD_TYPE "release")
endif ()

# 设置 debug 模式下编译输出
if (CMAKE_BUILD_TYPE MATCHES Debug)
    set(BUILD_TYPE "debug")
    
    # 定义调试选项，默认都为 OFF
    option(DEBUG_LOG "Enable log debug output" OFF)
    option(RUNTIME_DEBUG_LOG "Enable log debug output" OFF)
    option(DEBUG_LIR "Enable LIR debug output" OFF)
    option(DEBUG_ASM "Enable ASM debug output" OFF)
    option(DEBUG_PARSER "Enable parser debug output" OFF)
    option(DEBUG_INTERVAL "Enable interval debug output" OFF)
    option(DEBUG_MACHO "Enable MACHO debug output" OFF)
    option(ASSERT_ERROR "Enable assert error" OFF)

    # 添加这行来查看 DEBUG_LIR 的值
    message("DEBUG_LIR value: ${DEBUG_LIR}") 

    # 根据选项添加对应的编译定义
    if(RUNTIME_DEBUG_LOG)
        add_compile_definitions(RUNTIME_DEBUG_LOG)
    endif()
    if(DEBUG_LOG)
        add_compile_definitions(DEBUG_LOG)
    endif()
    if(DEBUG_LIR)
        message("DEBUG_LIR is enabled")
        add_compile_definitions(DEBUG_LIR="${DEBUG_LIR}")
    endif()
    if(DEBUG_ASM)
        add_compile_definitions(DEBUG_ASM)
    endif()
    if(DEBUG_PARSER)
        add_compile_definitions(DEBUG_PARSER)
    endif()
    if(DEBUG_INTERVAL)
        message("DEBUG_INTERVAL is enabled ${DEBUG_INTERVAL}")
        add_compile_definitions(DEBUG_INTERVAL="${DEBUG_INTERVAL}")
    endif()
    if(DEBUG_MACHO)
        add_compile_definitions(DEBUG_MACHO)
    endif()
    if(ASSERT_ERROR)
        add_compile_definitions(ASSERT_ERROR)
    endif()
    
    message("cmake type is debug")
endif()

configure_file("${PROJECT_SOURCE_DIR}/config/config.h.in" "${PROJECT_SOURCE_DIR}/config/config.h")

aux_source_directory(${PROJECT_SOURCE_DIR}/cmd CMD)
file(GLOB_RECURSE SRC ${PROJECT_SOURCE_DIR}/src/*.c)
aux_source_directory(${PROJECT_SOURCE_DIR}/utils UTILS)
aux_source_directory(${PROJECT_SOURCE_DIR}/utils/ymal UTILS)
aux_source_directory(${PROJECT_SOURCE_DIR}/utils/llhttp UTILS)

# include 搜索路径, 有了这个就不需要在 add_executable 中声明 .h 文件了
include_directories(./include)
include_directories(.)

add_executable(nature main.c ${SRC} ${CMD} ${UTILS})
target_link_libraries(nature m) # 链接数学计算库

# custom golang build package npkg
set(PACKAGE_BINARY_NAME npkg)
set(GOBIN /usr/local/go/bin/go)
set(PACKAGE_BINARY_PATH ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_BINARY_NAME})

add_custom_target(${PACKAGE_BINARY_NAME} ALL
        COMMAND GOOS=${CMAKE_SYSTEM_NAME} GOARCH=${CMAKE_SYSTEM_PROCESSOR} ${GOBIN} build -o ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_BINARY_NAME} ${PROJECT_SOURCE_DIR}/package/main.go
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/package
        COMMENT "package manager npkg"
)

# custom rust build package nls
#CARGO_TARGET_DIR=/custom/path cargo build --release
set(LSP_BINARY_NAME nls)
if(DEFINED RUST_TARGET)
    add_custom_target(${LSP_BINARY_NAME} ALL
            COMMAND CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR} cargo build --release --target ${RUST_TARGET}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/nls
            COMMENT "nature language server nls"
    )

    set(LSP_BINARY_PATH ${CMAKE_CURRENT_BINARY_DIR}/${RUST_TARGET}/release/${LSP_BINARY_NAME})
else()
    add_custom_target(${LSP_BINARY_NAME} ALL
            COMMAND CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR} cargo build --release
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/nls
            COMMENT "nature language server nls"
    )

    set(LSP_BINARY_PATH ${CMAKE_CURRENT_BINARY_DIR}/release/${LSP_BINARY_NAME})
endif()


enable_testing()
add_subdirectory(tests)
add_subdirectory(runtime EXCLUDE_FROM_ALL)


#--------------------------------- install ---------------------------------
install(TARGETS nature DESTINATION nature/bin)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_BINARY_NAME} DESTINATION nature/bin)
install(PROGRAMS ${LSP_BINARY_PATH} DESTINATION nature/bin)
install(DIRECTORY lib/ DESTINATION nature/lib)
install(DIRECTORY std/ DESTINATION nature/std)
install(FILES LICENSE-MIT DESTINATION nature/)
install(FILES LICENSE-APACHE DESTINATION nature/)
install(FILES VERSION DESTINATION nature/)


include(cmake/cpack.cmake)

