cmake_minimum_required(VERSION 3.21)

if(NOT IS_ABSOLUTE "${SED_SOURCE_COMMIT_TEST_ROOT}")
  message(FATAL_ERROR "An absolute SED_SOURCE_COMMIT_TEST_ROOT is required")
endif()
if(NOT DEFINED SED_SOURCE_COMMIT_STAGE_SCRIPT)
  set(SED_SOURCE_COMMIT_STAGE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../stage_release_package.cmake")
endif()
file(MAKE_DIRECTORY "${SED_SOURCE_COMMIT_TEST_ROOT}/inputs")
file(WRITE "${SED_SOURCE_COMMIT_TEST_ROOT}/inputs/editor" "synthetic executable")
file(WRITE "${SED_SOURCE_COMMIT_TEST_ROOT}/inputs/data.txt" "synthetic data")
file(WRITE "${SED_SOURCE_COMMIT_TEST_ROOT}/inputs/LICENSE" "synthetic notice")
set(_commit "0123456789abcdef0123456789abcdef01234567")

function(stage CASE COMMIT EXPECT_SUCCESS)
  set(_output "${SED_SOURCE_COMMIT_TEST_ROOT}/${CASE}")
  file(MAKE_DIRECTORY "${_output}")
  file(WRITE "${_output}/prior-stage.txt" "preserve on refusal")
  execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DSED_PACKAGE_EXECUTABLE=${SED_SOURCE_COMMIT_TEST_ROOT}/inputs/editor"
    "-DSED_PACKAGE_DATA_DIR=${SED_SOURCE_COMMIT_TEST_ROOT}/inputs"
    "-DSED_PACKAGE_RUNTIME_DATA=data.txt"
    "-DSED_PACKAGE_PROJECT_LICENSE_FILES=${SED_SOURCE_COMMIT_TEST_ROOT}/inputs/LICENSE"
    "-DSED_PACKAGE_OUTPUT_DIR=${_output}"
    -DSED_PACKAGE_PLATFORM=Linux -DSED_PACKAGE_MSVC=FALSE -DSED_PACKAGE_VERSION=1.2.3
    "-DSED_PACKAGE_SOURCE_COMMIT=${COMMIT}"
    -P "${SED_SOURCE_COMMIT_STAGE_SCRIPT}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 20)
  if(EXPECT_SUCCESS)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "${CASE} staging failed: ${_result}\n${_stdout}\n${_stderr}")
    endif()
    file(READ "${_output}/RELEASE_MANIFEST.json" _manifest)
    string(JSON _observed ERROR_VARIABLE _error GET "${_manifest}" sourceCommit)
    if(COMMIT STREQUAL "")
      if(_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR "A legacy unpinned stage must not invent a source commit")
      endif()
    elseif(NOT _error STREQUAL "NOTFOUND" OR NOT _observed STREQUAL COMMIT)
      message(FATAL_ERROR "The manifest must retain the exact supplied source commit")
    endif()
  else()
    if(_result EQUAL 0)
      message(FATAL_ERROR "Invalid source commit unexpectedly staged: ${CASE}")
    endif()
    if(NOT EXISTS "${_output}/prior-stage.txt")
      message(FATAL_ERROR "Invalid source metadata destroyed a prior stage: ${CASE}")
    endif()
    if(NOT _stderr MATCHES "SED_PACKAGE_SOURCE_COMMIT")
      message(FATAL_ERROR "${CASE} failed for an unrelated reason: ${_stderr}")
    endif()
  endif()
endfunction()

stage(pinned "${_commit}" TRUE)
stage(legacy "" TRUE)
stage(short "0123456" FALSE)
stage(nonhex "g123456789abcdef0123456789abcdef01234567" FALSE)
stage(uppercase "0123456789ABCDEF0123456789ABCDEF01234567" FALSE)
stage(injection "${_commit}\"" FALSE)
