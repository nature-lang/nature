if (NOT DEFINED NAX_BIN OR NOT DEFINED NAX_ROOT OR
    NOT DEFINED FIXTURE_DIR OR NOT DEFINED OUTPUT_EXE)
    message(FATAL_ERROR "Windows C/Nax ABI test arguments are incomplete")
endif ()

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "NAX_ROOT=${NAX_ROOT}"
                "${NAX_BIN}" build --target windows_amd64
                -o "${OUTPUT_EXE}" windows_c_nax_abi.n
        WORKING_DIRECTORY "${FIXTURE_DIR}"
        RESULT_VARIABLE BUILD_RESULT
        OUTPUT_VARIABLE BUILD_STDOUT
        ERROR_VARIABLE BUILD_STDERR)
if (NOT BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR
            "Nax Windows ABI fixture build failed (${BUILD_RESULT})\n"
            "${BUILD_STDOUT}\n${BUILD_STDERR}")
endif ()

execute_process(
        COMMAND "${OUTPUT_EXE}"
        WORKING_DIRECTORY "${FIXTURE_DIR}"
        RESULT_VARIABLE RUN_RESULT
        OUTPUT_VARIABLE RUN_STDOUT
        ERROR_VARIABLE RUN_STDERR)
if (NOT RUN_RESULT EQUAL 0)
    message(FATAL_ERROR
            "Nax Windows ABI fixture failed (${RUN_RESULT})\n"
            "${RUN_STDOUT}\n${RUN_STDERR}")
endif ()
