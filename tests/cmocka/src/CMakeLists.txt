project(cmocka-library C)

set(CMOCKA_PLATFORM_INCLUDE CACHE PATH "Path to include directory for cmocka_platform.h")
mark_as_advanced(CMOCKA_PLATFORM_INCLUDE)

set(CMOCKA_SHARED_LIBRARY
    cmocka
    CACHE INTERNAL "cmocka shared library"
)

if (BUILD_STATIC_LIB)
    set(CMOCKA_STATIC_LIBRARY
        cmocka-static
        CACHE INTERNAL "cmocka static library"
    )
endif()

set(CMOCKA_LINK_LIBRARIES
    ${CMOCKA_REQUIRED_LIBRARIES}
    CACHE INTERNAL "cmocka link libraries"
)

set(cmocka_SRCS
    cmocka.c
)

if (WIN32)
    set(cmocka_SRCS
        ${cmocka_SRCS}
        cmocka.def
    )
endif (WIN32)

add_library(${CMOCKA_SHARED_LIBRARY} SHARED ${cmocka_SRCS})

target_include_directories(${CMOCKA_SHARED_LIBRARY}
                           PRIVATE
                               ${CMOCKA_PLATFORM_INCLUDE}
                               ${cmocka_BINARY_DIR}
                           PUBLIC
                               ${cmocka-header_SOURCE_DIR})


target_compile_options(${CMOCKA_SHARED_LIBRARY}
                       PRIVATE
                           ${DEFAULT_C_COMPILE_FLAGS}
                           -DHAVE_CONFIG_H)
if (CMOCKA_PLATFORM_INCLUDE)
    target_compile_options(${CMOCKA_SHARED_LIBRARY}
                           PRIVATE
                               -DCMOCKA_PLATFORM_INCLUDE)
endif()

target_link_libraries(${CMOCKA_SHARED_LIBRARY} ${CMOCKA_LINK_LIBRARIES})
set_property(TARGET
                 ${CMOCKA_SHARED_LIBRARY}
             PROPERTY
                 DEFINE_SYMBOL
                     CMOCKA_EXPORTS
             PROPERTY
                 LINKER_FLAGS
                     "${DEFAULT_LINK_FLAGS}")

if (NOT WIN32)
    set_target_properties(
        ${CMOCKA_SHARED_LIBRARY}
            PROPERTIES
                VERSION
                    ${LIBRARY_VERSION}
                SOVERSION
                    ${LIBRARY_SOVERSION}
    )
endif (NOT WIN32)

install(TARGETS
            ${CMOCKA_SHARED_LIBRARY}
        ARCHIVE DESTINATION
            ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION
            ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION
            ${CMAKE_INSTALL_BINDIR}
        COMPONENT
            ${PROJECT_NAME})

if (BUILD_STATIC_LIB)
    add_library(${CMOCKA_STATIC_LIBRARY} STATIC ${cmocka_SRCS})

    target_include_directories(${CMOCKA_STATIC_LIBRARY}
                               PRIVATE
                                   ${CMOCKA_PLATFORM_INCLUDE}
                                   ${cmocka_BINARY_DIR}
                               PUBLIC
                                   ${cmocka-header_SOURCE_DIR})

    target_compile_options(${CMOCKA_STATIC_LIBRARY}
                           PRIVATE
                               ${DEFAULT_C_COMPILE_FLAGS}
                               -DHAVE_CONFIG_H)
    if (CMOCKA_PLATFORM_INCLUDE)
        target_compile_options(${CMOCKA_STATIC_LIBRARY}
                               PRIVATE
                                   -DCMOCKA_PLATFORM_INCLUDE)
    endif()

    set_property(TARGET
                     ${CMOCKA_STATIC_LIBRARY}
                 PROPERTY
                     DEFINE_SYMBOL
                         CMOCKA_EXPORTS
                 PROPERTY
                     LINK_FLAGS
                        "${DEFAULT_LINK_FLAGS}")

    if (WITH_STATIC_LIB)
        install(TARGETS
                    ${CMOCKA_STATIC_LIBRARY}
                ARCHIVE DESTINATION
                    ${CMAKE_INSTALL_LIBDIR}
                COMPONENT
                    ${PROJECT_NAME})
    endif()
endif (BUILD_STATIC_LIB)
