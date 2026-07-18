if (NOT DEFINED NATURE_BIN OR NOT DEFINED NATURE_ROOT OR
    NOT DEFINED FIXTURE_DIR OR NOT DEFINED OUTPUT_EXE)
    message(FATAL_ERROR "Windows C/Nature ABI test arguments are incomplete")
endif ()

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "NATURE_ROOT=${NATURE_ROOT}"
                "${NATURE_BIN}" build --target windows_amd64
                -o "${OUTPUT_EXE}" main.n
        WORKING_DIRECTORY "${FIXTURE_DIR}"
        RESULT_VARIABLE BUILD_RESULT
        OUTPUT_VARIABLE BUILD_STDOUT
        ERROR_VARIABLE BUILD_STDERR)
if (NOT BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR
            "Nature Windows ABI fixture build failed (${BUILD_RESULT})\n"
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
            "Nature Windows ABI fixture failed (${RUN_RESULT})\n"
            "${RUN_STDOUT}\n${RUN_STDERR}")
endif ()
