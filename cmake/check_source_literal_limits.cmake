cmake_minimum_required(VERSION 3.21)

# MSVC rejects any single string-literal piece longer than 16380 bytes with
# C2026 (Visual Studio 2022 and older), before adjacent pieces are concatenated.
# Embedded GLSL and fixtures therefore split long raw strings into adjacent
# pieces. This portable check keeps that floor visible on non-MSVC builds.
#
# Inputs:
#   SED_LITERAL_SCAN_ROOTS  '|'-separated directories to scan recursively.
#   SED_LITERAL_LIMIT       Optional per-piece byte limit (default 16380).
#
# Scope: C++ raw string literals, including encoding-prefixed forms. Line
# endings are normalized to one byte, matching translation phase 1.

if(NOT DEFINED SED_LITERAL_SCAN_ROOTS OR SED_LITERAL_SCAN_ROOTS STREQUAL "")
  message(FATAL_ERROR "SED_LITERAL_SCAN_ROOTS must be provided")
endif()
if(NOT DEFINED SED_LITERAL_LIMIT OR SED_LITERAL_LIMIT STREQUAL "")
  set(SED_LITERAL_LIMIT 16380)
endif()

string(REPLACE "|" ";" _sed_literal_roots "${SED_LITERAL_SCAN_ROOTS}")

set(_sed_literal_violation_count 0)
set(_sed_literal_file_count 0)
set(_sed_literal_piece_count 0)

foreach(_sed_root IN LISTS _sed_literal_roots)
  if(NOT IS_DIRECTORY "${_sed_root}")
    message(FATAL_ERROR "Literal scan root is not a directory: ${_sed_root}")
  endif()

  file(GLOB_RECURSE _sed_files LIST_DIRECTORIES false
    "${_sed_root}/*.cpp"
    "${_sed_root}/*.cc"
    "${_sed_root}/*.cxx"
    "${_sed_root}/*.hpp"
    "${_sed_root}/*.h"
    "${_sed_root}/*.hh"
    "${_sed_root}/*.inl"
    "${_sed_root}/*.ipp")
  list(SORT _sed_files)

  foreach(_sed_file IN LISTS _sed_files)
    math(EXPR _sed_literal_file_count "${_sed_literal_file_count} + 1")
    file(READ "${_sed_file}" _sed_content)
    string(REPLACE "\r\n" "\n" _sed_content "${_sed_content}")

    set(_sed_consumed 0)
    set(_sed_rest "${_sed_content}")
    while(TRUE)
      # Raw strings start at an identifier boundary with an optional encoding
      # prefix. The delimiter excludes spaces, parentheses, backslashes,
      # quotes, and control characters. The leftmost regex match is also the
      # first occurrence of its text, so FIND recovers its offset.
      string(REGEX MATCH "(^|[^A-Za-z0-9_])((u8|u|U|L)?R)\"([^ ()\\\\\t\n\"]*)\\("
        _sed_open "${_sed_rest}")
      if(_sed_open STREQUAL "")
        break()
      endif()
      set(_sed_boundary "${CMAKE_MATCH_1}")
      set(_sed_delimiter "${CMAKE_MATCH_4}")
      string(FIND "${_sed_rest}" "${_sed_open}" _sed_match_start)
      string(LENGTH "${_sed_boundary}" _sed_boundary_length)
      math(EXPR _sed_marker "${_sed_match_start} + ${_sed_boundary_length}")

      string(LENGTH "${_sed_open}" _sed_open_length)
      math(EXPR _sed_body_start "${_sed_match_start} + ${_sed_open_length}")
      string(LENGTH "${_sed_delimiter}" _sed_delimiter_length)
      set(_sed_body_length -1)
      if(_sed_delimiter_length LESS_EQUAL 16)
        string(SUBSTRING "${_sed_rest}" ${_sed_body_start} -1 _sed_body)
        string(FIND "${_sed_body}" ")${_sed_delimiter}\"" _sed_body_length)
      endif()
      if(_sed_body_length EQUAL -1)
        # Not a well-formed raw string. Skip the marker and keep scanning;
        # the compiler diagnoses genuinely malformed input.
        math(EXPR _sed_skip "${_sed_marker} + 2")
        string(SUBSTRING "${_sed_rest}" ${_sed_skip} -1 _sed_rest)
        math(EXPR _sed_consumed "${_sed_consumed} + ${_sed_skip}")
        continue()
      endif()

      math(EXPR _sed_literal_piece_count "${_sed_literal_piece_count} + 1")
      if(_sed_body_length GREATER SED_LITERAL_LIMIT)
        math(EXPR _sed_absolute_marker "${_sed_consumed} + ${_sed_marker}")
        string(SUBSTRING "${_sed_content}" 0 ${_sed_absolute_marker} _sed_prefix)
        string(REGEX MATCHALL "\n" _sed_newlines "${_sed_prefix}")
        list(LENGTH _sed_newlines _sed_line)
        math(EXPR _sed_line "${_sed_line} + 1")
        math(EXPR _sed_literal_violation_count "${_sed_literal_violation_count} + 1")
        message(NOTICE
          "${_sed_file}:${_sed_line}: raw string literal piece has ${_sed_body_length} bytes (limit ${SED_LITERAL_LIMIT})")
      endif()

      math(EXPR _sed_skip
        "${_sed_body_start} + ${_sed_body_length} + ${_sed_delimiter_length} + 2")
      string(SUBSTRING "${_sed_rest}" ${_sed_skip} -1 _sed_rest)
      math(EXPR _sed_consumed "${_sed_consumed} + ${_sed_skip}")
    endwhile()
  endforeach()
endforeach()

if(_sed_literal_violation_count GREATER 0)
  message(FATAL_ERROR
    "${_sed_literal_violation_count} oversized string-literal piece(s) would fail MSVC C2026. Split each listed literal into adjacent pieces at line boundaries.")
endif()

message(STATUS
  "Literal limit check passed: ${_sed_literal_piece_count} raw string pieces in ${_sed_literal_file_count} files are within ${SED_LITERAL_LIMIT} bytes")
