cmake_minimum_required(VERSION 3.24)
project(STSat VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# ST Shell: use a sibling checkout (../st-shell) when present, otherwise fetch
# the private repo. Override with -DST_SHELL_DIR=/path/to/st-shell.
# ---------------------------------------------------------------------------
set(ST_SHELL_DIR "" CACHE PATH "Path to a local st-shell checkout")
set(ST_SHELL_TAG "main" CACHE STRING "st-shell git ref to fetch")

if(ST_SHELL_DIR STREQUAL "" AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../st-shell/CMakeLists.txt")
    set(ST_SHELL_DIR "${CMAKE_CURRENT_LIST_DIR}/../st-shell")
endif()

if(NOT ST_SHELL_DIR STREQUAL "")
    message(STATUS "Using local st-shell at ${ST_SHELL_DIR}")
    add_subdirectory("${ST_SHELL_DIR}" "${CMAKE_BINARY_DIR}/_deps/st-shell-build")
else()
    include(FetchContent)
    FetchContent_Declare(st_shell
        GIT_REPOSITORY https://github.com/davidstokarski/st-shell.git
        GIT_TAG        ${ST_SHELL_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(st_shell)
endif()

file(GLOB_RECURSE STSAT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.h")

st_add_plugin(STSat
    PRODUCT_NAME  "ST Sat"
    PLUGIN_CODE   Stst
    VERSION       ${PROJECT_VERSION}
    BUNDLE_SUFFIX stsat
    SOURCES       ${STSAT_SOURCES})

target_include_directories(STSat PRIVATE "${CMAKE_CURRENT_LIST_DIR}/Source")

option(STSAT_BUILD_TESTS "Build offline DSP test tool" OFF)
if(STSAT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
