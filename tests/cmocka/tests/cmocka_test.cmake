#
# CTEST SCRIPT FOR CMOCKA TESTING
#

#
# Running this script:
#
#   ctest -S tests/cmocka_test.cmake \
#       -DCTEST_MODEL="Nightly"
#       -DCTEST_SITE="host.cmocka.org" \
#       -DCTEST_TARGET_SYSTEM="Linux-openSUSE_Tumbleweed-x86_64"
#
# The Target system describes the target OS, version, architecture, etc. This
# parameter allows the testing script to choose appropriate configuration for
# CMake and build tools.
#
# The set of supported targets is defined by the project.
#
# The generic format for the Target system is <KIND>[-<NAME>][-<ARCH>], where
#
#   <KIND> is one of Linux, Windows, MacOS, Android.
#   <NAME> is an optional OS name and version, for example Fedora-28, Win10.
#   <ARCH> is an optional architecture description, for example x86_64, ARM, ARM-Tegra5.
#
# To enable coverage, set:
#   -DCTEST_WITH_COVERAGE=TRUE
#
# To enable dynamic analysis, set:
#   -DCTEST_WITH_DYNAMIC_ANALYSIS=TRUE
#

#
# 0. Set defaults
#
set(PROJECT_NAME "cmocka")
set(PROJECT_GIT_URL "https://git.cryptomilk.org/projects/cmocka.git")

#
# 1. Include CText Ext module
#
if(NOT CTEST_EXT_INCLUDED)
    function(download_ctest_ext)
        message("Download latest version of CTest Extension module")

        find_package(Git QUIET)

        set(repo_url "https://github.com/jet47/ctest-ext.git")
        set(repo_dir "${CMAKE_CURRENT_LIST_DIR}/ctest-ext")
        set(tmp_dir "${CMAKE_CURRENT_LIST_DIR}/ctest-ext-tmp")

        if(NOT EXISTS "${repo_dir}")
            set(CTEST_CHECKOUT_COMMAND "${GIT_EXECUTABLE} clone ${repo_url} ${repo_dir}")
        endif()
        set(CTEST_UPDATE_COMMAND "${GIT_EXECUTABLE}")

        ctest_start("CTestExt" "${repo_dir}" "${tmp_dir}")
        ctest_update(SOURCE "${repo_dir}")

        file(REMOVE_RECURSE "${tmp_dir}")

        set(CTEST_EXT_MODULE_PATH "${repo_dir}" PARENT_SCOPE)
    endfunction()

    if(NOT DEFINED CTEST_EXT_MODULE_PATH)
        if(DEFINED ENV{CTEST_EXT_MODULE_PATH} AND EXISTS "$ENV{CTEST_EXT_MODULE_PATH}/ctest_ext.cmake")
            set(CTEST_EXT_MODULE_PATH "$ENV{CTEST_EXT_MODULE_PATH}")
        elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/ctest-ext/ctest_ext.cmake")
            set(CTEST_EXT_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/ctest-ext")
        else()
            download_ctest_ext()
        endif()
    endif()

    include("${CTEST_EXT_MODULE_PATH}/ctest_ext.cmake")
endif()

#
# 2. Initialize CTest Ext module
#
set_ifndef(CTEST_PROJECT_NAME ${PROJECT_NAME})

set_ifndef(CTEST_PROJECT_GIT_URL ${PROJECT_GIT_URL})
set_ifndef(CTEST_WITH_UPDATE TRUE)

ctest_ext_init()

#
# 3. Configure project for testing
#

# Check supported targets and models
check_if_matches(CTEST_TARGET_SYSTEM "^Linux" "^Windows")
check_if_matches(CTEST_MODEL "^Experimental$" "^Nightly$" "^Continuous$" "^Release$" "^Documentation$")

# Checks for Continuous model
set(IS_CONTINUOUS FALSE)
if(CTEST_MODEL MATCHES "Continuous")
    set(IS_CONTINUOUS TRUE)
endif()

set(IS_BINARY_EMPTY FALSE)
if(NOT EXISTS "${CTEST_BINARY_DIRECTORY}/CMakeCache.txt")
    set(IS_BINARY_EMPTY TRUE)
endif()

if(IS_CONTINUOUS AND NOT IS_BINARY_EMPTY AND NOT HAVE_UPDATES)
    ctest_ext_info("Continuous model : no updates")
    return()
endif()

# Configure the testing model
set_ifndef(CTEST_WITH_SUBMIT TRUE)

if(CTEST_MODEL MATCHES "Documentation")
    set_ifndef(CTEST_WITH_TESTS FALSE)
else()
    set_ifndef(CTEST_WITH_TESTS TRUE)
endif()


set_ifndef(CTEST_WITH_COVERAGE          FALSE)
if (CTEST_WITH_COVERAGE)
    set_ifndef(CTEST_COVERAGE_TOOL          "CDASH")
endif()

set_ifndef(CTEST_WITH_DYNAMIC_ANALYSIS  FALSE)
if (CTEST_WITH_DYNAMIC_ANALYSIS)
    set_ifndef(CTEST_DYNAMIC_ANALYSIS_TOOL  "CDASH")
endif()


if(CTEST_MODEL MATCHES "Continuous")
    set_ifndef(CTEST_EMPTY_BINARY_DIRECTORY FALSE)
else()
    set_ifndef(CTEST_EMPTY_BINARY_DIRECTORY TRUE)
endif()

# Set CMake options
if(CTEST_TARGET_SYSTEM MATCHES "Windows")
    if(CTEST_TARGET_SYSTEM MATCHES "64")
        set_ifndef(CTEST_CMAKE_GENERATOR "Visual Studio 13 Win64")
    else()
        set_ifndef(CTEST_CMAKE_GENERATOR "Visual Studio 13")
    endif()
else()
    set_ifndef(CTEST_CMAKE_GENERATOR "Unix Makefiles")
endif()

if(CTEST_MODEL MATCHES "(Release|Continuous)")
    set_ifndef(CTEST_CONFIGURATION_TYPE "Release")
else()
    set_ifndef(CTEST_CONFIGURATION_TYPE "Debug")
endif()

### Add project default build options here !!!

add_cmake_cache_entry(UNIT_TESTING ON)
add_cmake_cache_entry(WITH_CMOCKERY_SUPPORT ON)

### Add project default build options here ^^^

add_cmake_cache_entry("ENABLE_CTEST" TYPE "BOOL" "ON")

if(CTEST_WITH_COVERAGE)
    add_cmake_cache_entry("ENABLE_COVERAGE" TYPE "BOOL" "ON")
else()
    add_cmake_cache_entry("ENABLE_COVERAGE" TYPE "BOOL" "OFF")
endif()

if(CTEST_MODEL MATCHES "Documentation")
    add_cmake_cache_entry("BUILD_DOCS" TYPE "BOOL" "ON")
endif()

if(CTEST_MODEL MATCHES "Release")
    if(CTEST_TARGET_SYSTEM MATCHES "Windows")
        add_cmake_cache_entry("CPACK_GENERATOR" TYPE "STRING" "ZIP")
    else()
        add_cmake_cache_entry("CPACK_GENERATOR" TYPE "STRING" "TGZ")
    endif()
endif()

#
# 4. Start testing, configure and build project
#
ctest_ext_start()

ctest_ext_configure()

if(CTEST_MODEL MATCHES "Release")
    ctest_ext_build(TARGETS "ALL" "package")
elseif(CTEST_MODEL MATCHES "Documentation")
    ctest_ext_build(TARGET "docs")
else()
    ctest_ext_build()
endif()

#
# 5. Run tests
#
if(CTEST_MODEL MATCHES "Nightly")
    ctest_ext_test(INCLUDE_LABEL "Full")
else()
    ctest_ext_test(EXCLUDE_LABEL "Light")
endif()

ctest_ext_coverage(
    CDASH
        LABELS "Module"
)

ctest_ext_dynamic_analysis(
    CDASH
        INCLUDE_LABEL "Light"
)

#
# 6. Submit results to remote server
#
if(CTEST_MODEL MATCHES "Release")
    if(CTEST_TARGET_SYSTEM MATCHES "Windows")
        file(GLOB packages "${CTEST_BINARY_DIRECTORY}/*.zip")
    else()
        file(GLOB packages "${CTEST_BINARY_DIRECTORY}/*.tar.gz")
    endif()

    list(APPEND CTEST_UPLOAD_FILES ${packages})
endif()

ctest_ext_submit()
