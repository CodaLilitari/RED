cmake_minimum_required(VERSION 3.21)

project(
  SED
  VERSION 0.4.0
  DESCRIPTION "Modern WYSIWYG level editor for Sith-engine games"
  HOMEPAGE_URL "https://github.com/smlu/SED"
  LANGUAGES CXX
)

set(SED_VERSION_SUFFIX "")
set(SED_DISPLAY_VERSION "v${PROJECT_VERSION}")
if(SED_VERSION_SUFFIX)
  string(APPEND SED_DISPLAY_VERSION " - ${SED_VERSION_SUFFIX}")
endif()

set(_sed_default_cpp_standard 23)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  set(_sed_default_cpp_standard 26)
endif()

set(SED_CPP_STANDARD "${_sed_default_cpp_standard}" CACHE STRING "Preferred C++ standard: 26, 23, or 20.")
set_property(CACHE SED_CPP_STANDARD PROPERTY STRINGS 26 23 20)
option(SED_CPP_USE_CPP23 "Legacy option: build sed_cpp with at least C++23" OFF)
option(SED_BUILD_APP "Build Qt editor app" ON)
option(SED_BUILD_AGENT "Build headless sed-agent CLI" ON)
option(SED_BUILD_MCP "Build stdio MCP adapter sed-mcp" ON)
option(SED_BUILD_UPDATE "Build adapted updater tool sed-update" ON)
option(SED_BUILD_PLUGIN_BRIDGE "Build the out-of-process JED plug-in bridge" ON)
option(SED_BUILD_FUZZERS "Build Clang libFuzzer targets for untrusted binary inputs" OFF)

set(_sed_suppress_regen_default OFF)
if(CMAKE_GENERATOR MATCHES "Visual Studio")
  set(_sed_suppress_regen_default ON)
endif()

if (MSVC)
    add_link_options(
        "$<$<AND:$<CONFIG:Release>,$<LINK_LANGUAGE:CXX>>:/Brepro>")

    if (CMAKE_CONFIGURATION_TYPES)
        message(STATUS "Configuring MSVC build with C++ Hot Reload flags for Debug configuration only.")

        add_compile_options(
            "$<$<CONFIG:Debug>:/ZI>"
            "$<$<CONFIG:Debug>:/Od>"
            "$<$<CONFIG:Debug>:/RTC1>"
            "$<$<CONFIG:Debug>:/Ob0>")

        add_link_options(
            "$<$<CONFIG:Debug>:/INCREMENTAL>")
    elseif (CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
        message(STATUS "Configuring for C++ Hot Reload (Debug)...")

        # /ZI = Debug Info for Edit and Continue
        # /Od = Disable optimizations (required for Hot Reload)
        add_compile_options(/ZI /Od /RTC1 /Ob0)

        # /INCREMENTAL = Required for patching the binary
        add_link_options(/INCREMENTAL)
    endif()
endif()

option(SED_SUPPRESS_REGENERATION "Disable CMake regeneration target (workaround for VS stamp issues)" ${_sed_suppress_regen_default})
if(SED_SUPPRESS_REGENERATION)
  set(CMAKE_SUPPRESS_REGENERATION ON)
endif()

set(_sed_supported_cpp_standards 20 23 26)
list(FIND _sed_supported_cpp_standards "${SED_CPP_STANDARD}" _sed_cpp_standard_index)
if(_sed_cpp_standard_index EQUAL -1)
  message(FATAL_ERROR "SED_CPP_STANDARD must be one of: 26, 23, 20")
endif()

if(SED_CPP_USE_CPP23 AND SED_CPP_STANDARD STREQUAL "20")
  set(SED_CPP_STANDARD 23 CACHE STRING "Preferred C++ standard: 26, 23, or 20." FORCE)
endif()

if(SED_CPP_STANDARD STREQUAL "26" AND CMAKE_VERSION VERSION_LESS 3.28)
  message(STATUS "This CMake does not model C++26; falling back to C++23.")
  set(CMAKE_CXX_STANDARD 23)
else()
  set(CMAKE_CXX_STANDARD ${SED_CPP_STANDARD})
endif()

# Prefer the requested standard but allow CMake/compiler fallback to the newest
# supported mode. Targets still require at least C++20 through compile features.
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Every raw-string literal piece must stay within MSVC's C2026 limit so the
# documented Visual Studio 2022 floor keeps compiling on every host.
add_test(NAME sed_source_literal_limits
  COMMAND "${CMAKE_COMMAND}"
    "-DSED_LITERAL_SCAN_ROOTS=${CMAKE_CURRENT_SOURCE_DIR}/src|${CMAKE_CURRENT_SOURCE_DIR}/tools"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_source_literal_limits.cmake"
)
add_test(NAME sed_source_literal_limits_tests
  COMMAND "${CMAKE_COMMAND}"
    "-DSED_LITERAL_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/source-literal-limit-tests"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/check_source_literal_limits_tests.cmake"
)
set_tests_properties(sed_source_literal_limits PROPERTIES
  LABELS "system;contracts"
  TIMEOUT 120
)
set_tests_properties(sed_source_literal_limits_tests PROPERTIES
  LABELS "module;contracts"
  TIMEOUT 60
)

add_subdirectory(src/core)
if(SED_BUILD_PLUGIN_BRIDGE)
  add_subdirectory(tools/red-jed-plugin-bridge)
endif()
if(SED_BUILD_APP)
  add_subdirectory(src/app)
endif()
if(SED_BUILD_AGENT)
  add_subdirectory(tools/sed-agent)
endif()
if(SED_BUILD_MCP)
  add_subdirectory(tools/sed-mcp)
endif()
if(SED_BUILD_AGENT)
  add_subdirectory(tools/sed-cpp-bridge)
endif()
if(SED_BUILD_UPDATE)
  add_subdirectory(tools/sed-update)
endif()
add_subdirectory(tools/engine-capture)
