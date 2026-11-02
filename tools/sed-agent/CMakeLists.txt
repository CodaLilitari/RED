add_executable(sed_agent
  main.cpp
  agent_json.cpp
  agent_workspace.cpp
)
target_link_libraries(sed_agent PRIVATE sed_core)
target_compile_features(sed_agent PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sed_agent PRIVATE /FS)
endif()

add_executable(sed_agent_tests tests/agent_cli_tests.cpp)
target_compile_features(sed_agent_tests PRIVATE cxx_std_20)
target_compile_definitions(sed_agent_tests PRIVATE
  "SED_AGENT_EXE=\"$<TARGET_FILE:sed_agent>\"")
add_dependencies(sed_agent_tests sed_agent)
if(MSVC)
  target_compile_options(sed_agent_tests PRIVATE /FS)
endif()

foreach(agent_scenario IN ITEMS exit-codes stale-hash existing-output request-cap plan-writes-nothing query select validate-new-convert archive-extract-launch snapshot-refusal cog-bindings cog-add-rebind light-rgb workbench)
  add_test(NAME "sed_agent_${agent_scenario}"
    COMMAND sed_agent_tests "${agent_scenario}")
  set_tests_properties("sed_agent_${agent_scenario}" PROPERTIES
    LABELS "integration;agent"
    TIMEOUT 60
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endforeach()
