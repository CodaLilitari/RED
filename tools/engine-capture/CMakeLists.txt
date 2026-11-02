# D-14 capture-runner pattern: stdlib Python, no build products.
# Register its unittest suite as a ctest entry when a Python 3
# interpreter is available; the runner itself is used directly.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME sed_engine_capture_runner
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -v)
  set_tests_properties(sed_engine_capture_runner PROPERTIES
    LABELS "integration;engine-capture"
    TIMEOUT 120
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
