cmake_minimum_required(VERSION 3.31)
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_SYSROOT)
    execute_process(COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE CMAKE_OSX_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
endif()
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APP_VERSION LIMIT_COUNT 1)
string(STRIP "${APP_VERSION}" APP_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(obsidian_mcp VERSION "${APP_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
include(CheckIPOSupported)
include(CheckCXXSourceCompiles)
option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

find_package(slimr-mcp CONFIG REQUIRED)

add_library(app_options INTERFACE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libc++.so
        OUTPUT_VARIABLE libcxx_so OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    cmake_path(GET libcxx_so PARENT_PATH libcxx_dir)
    cmake_path(NORMAL_PATH libcxx_dir)
    string(APPEND CMAKE_REQUIRED_FLAGS " -stdlib=libc++")
    list(APPEND CMAKE_REQUIRED_LINK_OPTIONS -L${libcxx_dir})
    target_compile_options(app_options INTERFACE -stdlib=libc++)
    target_link_options(app_options INTERFACE -stdlib=libc++ -L${libcxx_dir}
        $<$<CONFIG:Release>:-static-libstdc++> $<$<NOT:$<CONFIG:Release>>:-Wl$<COMMA>-rpath$<COMMA>${libcxx_dir}>)
elseif(APPLE)
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=c++/libc++.a
        OUTPUT_VARIABLE libcxx_a OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    cmake_path(NORMAL_PATH libcxx_a)
    if(NOT EXISTS "${libcxx_a}")
        message(FATAL_ERROR "Static libc++.a not found via ${CMAKE_CXX_COMPILER} (got '${libcxx_a}'); use llvm@23")
    endif()
    cmake_path(GET libcxx_a PARENT_PATH libcxx_dir)
    string(APPEND CMAKE_REQUIRED_FLAGS " -stdlib=libc++")
    list(APPEND CMAKE_REQUIRED_LINK_OPTIONS -nostdlib++ "${libcxx_dir}/libc++.a" "${libcxx_dir}/libc++abi.a")
    target_compile_options(app_options INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>")
    target_link_options(app_options INTERFACE
        -nostdlib++ "${libcxx_dir}/libc++.a" "${libcxx_dir}/libc++abi.a"
        -Wl,-exported_symbols_list,/dev/null)
endif()
target_compile_options(app_options INTERFACE -ftrivial-auto-var-init=zero)
check_cxx_source_compiles("
#include <version>
#ifndef _LIBCPP_VERSION
#error libc++ is required for the selected hardening mode
#endif
#ifndef _LIBCPP_HARDENING_MODE_FAST
#error libc++ FAST hardening is unavailable
#endif
int main() { return 0; }" HAVE_LIBCPP_HARDENING)
if(NOT HAVE_LIBCPP_HARDENING)
    message(FATAL_ERROR "Use a Clang/libc++ toolchain supporting FAST hardening (the check also fails on link errors; see CMakeFiles/CMakeConfigureLog.yaml)")
endif()
target_compile_definitions(app_options INTERFACE
    OBSIDIAN_APP_VERSION="${PROJECT_VERSION}"
    _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST)
if(ENABLE_SANITIZERS)
    target_compile_options(app_options INTERFACE
        -g -fsanitize=address,undefined -fno-sanitize-recover=undefined)
    target_link_options(app_options INTERFACE -fsanitize=address,undefined)
endif()
check_ipo_supported(RESULT HAVE_IPO OUTPUT IPO_ERROR)
if(NOT HAVE_IPO)
    message(FATAL_ERROR "Release IPO is required: ${IPO_ERROR}")
endif()

add_executable(obsidian-mcp src/main.cpp)
target_include_directories(obsidian-mcp PRIVATE src)
target_link_libraries(obsidian-mcp PRIVATE slimr::mcp_http app_options)
set_target_properties(obsidian-mcp PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/bin")

if(BUILD_TESTING)
    find_package(slimr-test CONFIG REQUIRED)
    add_executable(obsidian_mcp_test tests/test_main.cpp)
    target_include_directories(obsidian_mcp_test PRIVATE src tests)
    target_link_libraries(obsidian_mcp_test PRIVATE slimr::mcp_http slimr::test app_options)
    set_target_properties(obsidian_mcp_test PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    add_test(NAME obsidian_mcp COMMAND obsidian_mcp_test)
    set_tests_properties(obsidian_mcp PROPERTIES TIMEOUT 120
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "BUGSINK_DSN=;TMPDIR=${CMAKE_BINARY_DIR}")
endif()
