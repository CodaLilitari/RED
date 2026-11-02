# sed-mcp is a thin MCP transport over the F-01 agent layer. It compiles
# the sed-agent workspace/JSON units directly (same operations, same
# code) and links sed_core only; it must never grow domain logic.
add_executable(sed_mcp
  main.cpp
  ../sed-agent/agent_json.cpp
  ../sed-agent/agent_workspace.cpp
)
target_include_directories(sed_mcp PRIVATE ../sed-agent)
target_link_libraries(sed_mcp PRIVATE sed_core)
target_compile_features(sed_mcp PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sed_mcp PRIVATE /FS)
endif()

add_executable(sed_mcp_tests tests/mcp_stdio_tests.cpp)
target_compile_features(sed_mcp_tests PRIVATE cxx_std_20)
target_compile_definitions(sed_mcp_tests PRIVATE
  "SED_MCP_EXE=\"$<TARGET_FILE:sed_mcp>\"")
add_dependencies(sed_mcp_tests sed_mcp)
if(MSVC)
  target_compile_options(sed_mcp_tests PRIVATE /FS)
endif()

foreach(mcp_scenario IN ITEMS protocol writes outcome snapshot-policy)
  add_test(NAME "sed_agent_mcp_${mcp_scenario}"
    COMMAND sed_mcp_tests "${mcp_scenario}")
  set_tests_properties("sed_agent_mcp_${mcp_scenario}" PROPERTIES
    LABELS "integration;agent"
    TIMEOUT 60
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endforeach()
