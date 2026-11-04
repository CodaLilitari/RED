cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED SED_LITERAL_TEST_ROOT OR SED_LITERAL_TEST_ROOT STREQUAL "")
  message(FATAL_ERROR "SED_LITERAL_TEST_ROOT must be provided")
endif()

set(CHECK_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../check_source_literal_limits.cmake")
file(REMOVE_RECURSE "${SED_LITERAL_TEST_ROOT}")

# Writes a C++ file whose single raw-string piece holds exactly BODY_BYTES
# bytes after line-ending normalization. Lines are 63 'x' bytes plus a newline.
function(write_raw_piece_file PATH DELIMITER BODY_BYTES NEWLINE)
  set(_line "")
  string(REPEAT "x" 63 _line)
  math(EXPR _full_lines "${BODY_BYTES} / 64")
  math(EXPR _tail "${BODY_BYTES} % 64")
  string(REPEAT "${_line}${NEWLINE}" ${_full_lines} _body)
  if(_tail GREATER 0)
    string(REPEAT "y" ${_tail} _tail_text)
    string(APPEND _body "${_tail_text}")
  endif()
  file(WRITE "${PATH}"
    "// synthetic fixture\nconst char* piece = R\"${DELIMITER}(${_body})${DELIMITER}\";\n")
endfunction()

function(run_check CASE_NAME EXPECT_SUCCESS ROOTS)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DSED_LITERAL_SCAN_ROOTS=${ROOTS}"
      -P "${CHECK_SCRIPT}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
  if(EXPECT_SUCCESS AND NOT _result EQUAL 0)
    message(FATAL_ERROR
      "${CASE_NAME} unexpectedly failed (${_result}):\n${_output}\n${_error}")
  endif()
  if(NOT EXPECT_SUCCESS AND _result EQUAL 0)
    message(FATAL_ERROR "${CASE_NAME} unexpectedly passed:\n${_output}")
  endif()
  set(LAST_CHECK_OUTPUT "${_output}\n${_error}" PARENT_SCOPE)
endfunction()

set(ROOT "${SED_LITERAL_TEST_ROOT}")
file(MAKE_DIRECTORY
  "${ROOT}/limit"
  "${ROOT}/crlf"
  "${ROOT}/adjacent"
  "${ROOT}/markers"
  "${ROOT}/oversize/nested"
  "${ROOT}/oversize_crlf")

write_raw_piece_file("${ROOT}/limit/at_limit.cpp" "GLSL" 16380 "\n")
run_check(limit TRUE "${ROOT}/limit")
if(NOT LAST_CHECK_OUTPUT MATCHES "1 raw string pieces in 1 files")
  message(FATAL_ERROR "limit case did not inspect its raw piece:\n${LAST_CHECK_OUTPUT}")
endif()

# CRLF counts as one byte after translation phase 1, so 16380 normalized
# bytes stay legal even though the file holds more bytes on disk.
write_raw_piece_file("${ROOT}/crlf/at_limit_crlf.hpp" "" 16380 "\r\n")
run_check(crlf TRUE "${ROOT}/crlf")

string(REPEAT "z" 9000 _half)
file(WRITE "${ROOT}/adjacent/split.cpp"
  "const char* shader = R\"GLSL(${_half})GLSL\"\nR\"GLSL(${_half})GLSL\";\n")
run_check(adjacent TRUE "${ROOT}/adjacent")
if(NOT LAST_CHECK_OUTPUT MATCHES "2 raw string pieces in 1 files")
  message(FATAL_ERROR "adjacent case did not count both pieces:\n${LAST_CHECK_OUTPUT}")
endif()

# Ordinary strings ending in R must not be treated as raw-string openers.
string(REPEAT "q" 20000 _long_tail)
file(WRITE "${ROOT}/markers/ordinary.cpp"
  "const char* color = \"COLOR\"; const char* tag = \"OR\";\n// ${_long_tail}\n")
run_check(markers TRUE "${ROOT}/markers")
if(NOT LAST_CHECK_OUTPUT MATCHES "0 raw string pieces in 1 files")
  message(FATAL_ERROR "marker case misread ordinary strings:\n${LAST_CHECK_OUTPUT}")
endif()

write_raw_piece_file("${ROOT}/oversize/nested/too_big.cpp" "GLSL" 16381 "\n")
run_check(oversize FALSE "${ROOT}/oversize")
if(NOT LAST_CHECK_OUTPUT MATCHES "too_big\\.cpp:2: raw string literal piece has 16381 bytes")
  message(FATAL_ERROR "oversize case did not report file, line, and size:\n${LAST_CHECK_OUTPUT}")
endif()

write_raw_piece_file("${ROOT}/oversize_crlf/too_big_crlf.cpp" "SQL" 16381 "\r\n")
run_check(oversize_crlf FALSE "${ROOT}/oversize_crlf")

# A string ending in R followed by delimiter-like text is not a raw string and
# must not stop the scan before a later oversized literal.
file(MAKE_DIRECTORY "${ROOT}/false_opener" "${ROOT}/prefixed")
string(REPEAT "w" 16381 _oversize_body)
file(WRITE "${ROOT}/false_opener/after_false_opener.cpp"
  "call(\"COLOR\",bar(1));\nconst char* late = R\"GLSL(${_oversize_body})GLSL\";\n")
run_check(false_opener FALSE "${ROOT}/false_opener")
if(NOT LAST_CHECK_OUTPUT MATCHES "after_false_opener\\.cpp:2: raw string literal piece has 16381 bytes")
  message(FATAL_ERROR "false opener hid a later violation:\n${LAST_CHECK_OUTPUT}")
endif()

file(WRITE "${ROOT}/prefixed/encoded.cpp"
  "auto a = u8R\"x(${_oversize_body})x\";\nauto b = LR\"(short)\";\n")
run_check(prefixed FALSE "${ROOT}/prefixed")
if(NOT LAST_CHECK_OUTPUT MATCHES "encoded\\.cpp:1: raw string literal piece has 16381 bytes")
  message(FATAL_ERROR "encoding-prefixed raw string was not checked:\n${LAST_CHECK_OUTPUT}")
endif()

# Every root is scanned: a clean first root cannot hide a later violation.
run_check(multiple_roots FALSE "${ROOT}/limit|${ROOT}/oversize")

run_check(missing_root FALSE "${ROOT}/does-not-exist")

file(REMOVE_RECURSE "${SED_LITERAL_TEST_ROOT}")
message(STATUS "check_source_literal_limits self-tests passed")
