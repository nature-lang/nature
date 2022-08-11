project(cmocka-examples C)

function(ADD_CMOCKA_TEST_ENVIRONMENT _TARGET_NAME)
    if (WIN32 OR CYGWIN OR MINGW)
        file(TO_NATIVE_PATH "${cmocka-library_BINARY_DIR}" CMOCKA_DLL_PATH)

        if (TARGET_SYSTEM_EMULATOR)
            set(DLL_PATH_ENV "WINEPATH=${CMOCKA_DLL_PATH};$ENV{WINEPATH}")
        else()
            set(DLL_PATH_ENV "PATH=${CMOCKA_DLL_PATH};$ENV{PATH}")
        endif()
        #
        # IMPORTANT NOTE: The set_tests_properties(), below, internally
        # stores its name/value pairs with a semicolon delimiter.
        # because of this we must protect the semicolons in the path
        #
        string(REPLACE ";" "\\;" DLL_PATH_ENV "${DLL_PATH_ENV}")

        set_tests_properties(${_TARGET_NAME}
                             PROPERTIES
                                ENVIRONMENT
                                    "${DLL_PATH_ENV}")
    endif()
endfunction()

set_source_files_properties(calculator.c
                            allocate_module.c
                            assert_module.c
                            PROPERTIES
                                COMPILE_DEFINITIONS
                                    UNIT_TESTING=1)


### The most simple test
add_cmocka_test(simple_test
                SOURCES simple_test.c
                COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
                LINK_LIBRARIES ${CMOCKA_SHARED_LIBRARY})
add_cmocka_test_environment(simple_test)

### Allocate module test
add_cmocka_test(allocate_module_test
                SOURCES allocate_module.c allocate_module_test.c
                LINK_LIBRARIES ${CMOCKA_SHARED_LIBRARY})
add_cmocka_test_environment(allocate_module_test)

set_tests_properties(allocate_module_test
                     PROPERTIES
                        WILL_FAIL 1)

### Assert macro test
add_cmocka_test(assert_macro_test
                SOURCES assert_macro.c assert_macro_test.c
                LINK_LIBRARIES ${CMOCKA_SHARED_LIBRARY})
add_cmocka_test_environment(assert_macro_test)

set_tests_properties(assert_macro_test
                     PROPERTIES
                        WILL_FAIL 1)

### Assert module test
add_cmocka_test(assert_module_test
                SOURCES assert_module.c assert_module_test.c
                LINK_LIBRARIES ${CMOCKA_SHARED_LIBRARY})
add_cmocka_test_environment(assert_module_test)

set_tests_properties(assert_module_test
                     PROPERTIES
                        WILL_FAIL 1)

if (NOT WIN32)
    add_subdirectory(mock)
endif()
