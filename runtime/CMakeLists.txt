set(LIB_OUTPUT_PREFIX ${PROJECT_SOURCE_DIR}/lib)

message("runtime os: ${CMAKE_SYSTEM_NAME} arch: ${CMAKE_SYSTEM_PROCESSOR}")
add_library(runtime STATIC ${UTILS} ${RUNTIME_FILES})
set_target_properties(runtime PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${LIB_OUTPUT_PREFIX}/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR})
message("runtime register success")

