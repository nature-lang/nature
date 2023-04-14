set(LIB_OUTPUT_PREFIX ${PROJECT_SOURCE_DIR}/lib)

add_library(runtime STATIC runtime.h runtime.c)
set_target_properties(runtime PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY ${LIB_OUTPUT_PREFIX}/${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR})
