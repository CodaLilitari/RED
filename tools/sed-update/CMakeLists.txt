# sed-update is the adapted F-14 updater tool. It links sed_core only
# (no Qt), mirroring the sed-agent split: all update logic is core-testable.
add_executable(sed_update
  main.cpp
)
target_link_libraries(sed_update PRIVATE sed_core)
target_compile_features(sed_update PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sed_update PRIVATE /FS)
endif()

add_executable(sed_update_tests
  tests/update_cli_tests.cpp
  ../sed-agent/agent_json.cpp
)
target_link_libraries(sed_update_tests PRIVATE sed_core)
target_include_directories(sed_update_tests PRIVATE ../sed-agent)
target_compile_features(sed_update_tests PRIVATE cxx_std_20)
target_compile_definitions(sed_update_tests PRIVATE
  "SED_UPDATE_EXE=\"$<TARGET_FILE:sed_update>\"")
add_dependencies(sed_update_tests sed_update)
if(MSVC)
  target_compile_options(sed_update_tests PRIVATE /FS)
endif()

foreach(update_scenario IN ITEMS equal downgrade corrupt-hash interrupted-staging stage-activate json-output)
  add_test(NAME "sed_update_${update_scenario}"
    COMMAND sed_update_tests "${update_scenario}")
  set_tests_properties("sed_update_${update_scenario}" PROPERTIES
    LABELS "integration;agent"
    TIMEOUT 60
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endforeach()
