# sed-mcp is a thin MCP transport over the portable agent domain.
# Keep standalone MCP builds supported when SED_BUILD_AGENT is disabled.
# Domain logic remains shared with the CLI and never belongs in this transport.
if(NOT TARGET sed_agent_domain)
  add_library(sed_agent_domain STATIC
    ../sed-agent/agent_json.cpp
    ../sed-agent/agent_workspace.cpp
    ../sed-agent/agent_basic_operations.cpp
    ../sed-agent/agent_entity_operations.cpp
    ../sed-agent/agent_query_details.cpp
    ../sed-agent/agent_model_lighting.cpp
    ../sed-agent/agent_schema.cpp
    ../sed-agent/agent_edit_operations.cpp
    ../sed-agent/agent_arch_operations.cpp
    ../sed-agent/agent_semantic_preview.cpp
  )
  target_link_libraries(sed_agent_domain PUBLIC sed_core)
  target_compile_features(sed_agent_domain PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(sed_agent_domain PRIVATE /FS)
  endif()
endif()

add_executable(sed_mcp
  main.cpp
)
target_include_directories(sed_mcp PRIVATE ../sed-agent)
target_link_libraries(sed_mcp PRIVATE sed_agent_domain)
set_target_properties(sed_mcp PROPERTIES OUTPUT_NAME "red-mcp")  # RED
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

foreach(mcp_scenario IN ITEMS protocol writes thing-frames outcome snapshot-policy strict-validation package diagnose-fan model-lighting path-policy journal-policy)
  add_test(NAME "sed_agent_mcp_${mcp_scenario}"
    COMMAND sed_mcp_tests "${mcp_scenario}")
  set_tests_properties("sed_agent_mcp_${mcp_scenario}" PROPERTIES
    LABELS "integration;agent"
    TIMEOUT 60
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endforeach()

# Deterministic transport failure coverage with an isolated domain stub. No
# timing race or production failure-injection option is needed.
add_executable(sed_mcp_journal_completion_tests
  tests/mcp_stdio_tests.cpp
  ../sed-agent/agent_json.cpp
)
target_include_directories(sed_mcp_journal_completion_tests PRIVATE ../sed-agent)
target_link_libraries(sed_mcp_journal_completion_tests PRIVATE sed_core)
target_compile_features(sed_mcp_journal_completion_tests PRIVATE cxx_std_20)
target_compile_definitions(sed_mcp_journal_completion_tests PRIVATE SED_MCP_JOURNAL_COMPLETION_TEST)
if(MSVC)
  target_compile_options(sed_mcp_journal_completion_tests PRIVATE /FS)
endif()
add_test(NAME sed_agent_mcp_journal_completion COMMAND sed_mcp_journal_completion_tests)
set_tests_properties(sed_agent_mcp_journal_completion PROPERTIES LABELS "unit;agent" TIMEOUT 30)

add_executable(sed_mcp_parity_tests
  tests/mcp_parity_tests.cpp
)
target_include_directories(sed_mcp_parity_tests PRIVATE ../sed-agent)
target_link_libraries(sed_mcp_parity_tests PRIVATE sed_agent_domain)
target_compile_features(sed_mcp_parity_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sed_mcp_parity_tests PRIVATE /FS)
endif()
add_test(NAME sed_agent_mcp_parity COMMAND sed_mcp_parity_tests)
set_tests_properties(sed_agent_mcp_parity PROPERTIES LABELS "integration;agent;contracts" TIMEOUT 60)
