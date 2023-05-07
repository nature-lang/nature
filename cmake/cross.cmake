# 设置交叉编译默认名称(没有指定编译连链时触发)
if (CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(CMAKE_SYSTEM_NAME darwin)
    message("set CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(CMAKE_SYSTEM_NAME linux)
    message("set CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
endif ()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
    set(CMAKE_SYSTEM_PROCESSOR amd64)
    message("set CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}")
endif ()


# 写入到系统变量中
if (CMAKE_SYSTEM_NAME MATCHES darwin)
    message("add compile def __DARWIN")
    add_compile_definitions(__DARWIN)
else ()
    message("add compile def __LINUX")
    add_compile_definitions(__LINUX)
endif ()

if (CMAKE_SYSTEM_PROCESSOR MATCHES amd64)
    message("add compile def __AMD64")
    add_compile_definitions(__AMD64)
endif ()
