cmake_minimum_required(VERSION 3.21)

# Exercise the real generated target through the native build tool, including
# Windows' cmd.exe command-length limit. No Qt installation or compiler needed.
if(NOT DEFINED SED_TARGET_TEST_ROOT OR SED_TARGET_TEST_ROOT STREQUAL "")
  message(FATAL_ERROR "SED_TARGET_TEST_ROOT must be provided")
endif()
set(_source "${SED_TARGET_TEST_ROOT}/source with spaces")
set(_build "${SED_TARGET_TEST_ROOT}/build with spaces")
file(MAKE_DIRECTORY "${_source}")
set(_module "${CMAKE_CURRENT_LIST_DIR}/../release_package_target.cmake")
string(REPEAT "notice with spaces.dll|" 800 _long_notices)
file(WRITE "${_source}/expected.txt" "${_long_notices}")
set(_literal [====[notice "quoted";${DO_NOT_EXPAND};close]=]and]==]|C:\notices\x]====])
file(WRITE "${_source}/literal.txt" "${_literal}")
file(WRITE "${_source}/release_package.cmake" [=[
file(READ "${SED_PACKAGE_DATA_DIR}/../expected.txt" _expected)
if(NOT SED_RELEASE_QT_LICENSE_FILES STREQUAL _expected)
  message(FATAL_ERROR "The complete notice list did not reach the package script")
endif()
if(NOT SED_PACKAGE_CONFIGURATION STREQUAL "Release")
  message(FATAL_ERROR "Configuration generator expression was not resolved")
endif()
if(NOT EXISTS "${SED_PACKAGE_EXECUTABLE}")
  message(FATAL_ERROR "Executable generator expression was not resolved")
endif()
if(NOT SED_RELEASE_QT_REQUIRED_VERSION STREQUAL "")
  message(FATAL_ERROR "Empty setting changed during transport")
endif()
file(READ "${SED_PACKAGE_DATA_DIR}/../literal.txt" _literal_expected)
if(NOT SED_RELEASE_MSVC_NOTICE_FILES STREQUAL _literal_expected)
  message(FATAL_ERROR "Literal path characters changed during transport")
endif()
file(WRITE "${SED_PACKAGE_OUTPUT_DIR}" "${SED_RELEASE_QT_LICENSE_FILES}")
]=])
file(WRITE "${_source}/CMakeLists.txt"
  "cmake_minimum_required(VERSION 3.21)\n"
  "project(ReleaseTargetFixture VERSION 1.2.3 LANGUAGES NONE)\n"
  "add_executable(sed_app IMPORTED GLOBAL)\n"
  "set_target_properties(sed_app PROPERTIES IMPORTED_LOCATION [==[${CMAKE_COMMAND}]==])\n"
  "set(SED_RELEASE_PACKAGE_OUTPUT_DIR [==[${SED_TARGET_TEST_ROOT}/received.txt]==])\n"
  "file(READ [==[${_source}/expected.txt]==] SED_RELEASE_QT_LICENSE_FILES)\n"
  "file(READ [==[${_source}/literal.txt]==] SED_RELEASE_MSVC_NOTICE_FILES)\n"
  "include([==[${_module}]==])\n"
  "sed_add_release_package_target()\n")
if(NOT DEFINED SED_TARGET_TEST_GENERATOR)
  set(SED_TARGET_TEST_GENERATOR "Ninja Multi-Config")
endif()
set(_configure_args -G "${SED_TARGET_TEST_GENERATOR}")
if(DEFINED SED_TARGET_TEST_MAKE_PROGRAM AND NOT SED_TARGET_TEST_MAKE_PROGRAM STREQUAL "")
  list(APPEND _configure_args "-DCMAKE_MAKE_PROGRAM=${SED_TARGET_TEST_MAKE_PROGRAM}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_source}" -B "${_build}"
  ${_configure_args} -DCMAKE_BUILD_TYPE=Release RESULT_VARIABLE _configured
  OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _configured EQUAL 0)
  message(FATAL_ERROR "Package target fixture configure failed: ${_output}\n${_error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_build}"
  --config Release --target sed_release_package RESULT_VARIABLE _built
  OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _built EQUAL 0)
  message(FATAL_ERROR "Complete notice list could not reach staging: ${_output}\n${_error}")
endif()
file(READ "${SED_TARGET_TEST_ROOT}/received.txt" _received)
if(NOT _received STREQUAL _long_notices)
  message(FATAL_ERROR "Staging receipt differs from the complete notice list")
endif()
