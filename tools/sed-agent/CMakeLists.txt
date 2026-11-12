# CLI, contract tests and MCP share one portable domain implementation.
# Compiling these large units once also prevents target-specific drift.
add_library(sed_agent_domain STATIC
  agent_json.cpp
  agent_workspace.cpp
  agent_basic_operations.cpp
  agent_entity_operations.cpp
  agent_query_details.cpp
  agent_model_lighting.cpp
  agent_schema.cpp
  agent_edit_operations.cpp
  agent_arch_operations.cpp
  agent_semantic_preview.cpp
)
target_link_libraries(sed_agent_domain PUBLIC sed_core)
target_compile_features(sed_agent_domain PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(sed_agent_domain PRIVATE /FS)
endif()

add_executable(sed_agent
  main.cpp
)
target_link_libraries(sed_agent PRIVATE sed_agent_domain)
set_target_properties(sed_agent PROPERTIES OUTPUT_NAME "red-agent")  # RED
target_compile_features(sed_agent PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sed_agent PRIVATE /FS)
endif()

add_executable(sed_agent_tests tests/agent_cli_tests.cpp)
target_compile_features(sed_agent_tests PRIVATE cxx_std_20)
target_compile_definitions(sed_agent_tests PRIVATE
  "SED_AGENT_EXE=\"$<TARGET_FILE:sed_agent>\""
  "SED_BRIDGE_EXE=\"$<TARGET_FILE:sed_cpp_bridge>\"")
add_dependencies(sed_agent_tests sed_agent sed_cpp_bridge)
if(MSVC)
  target_compile_options(sed_agent_tests PRIVATE /FS)
endif()

foreach(agent_scenario IN ITEMS exit-codes stale-hash existing-output request-cap plan-writes-nothing query select validate-new-convert validate-strict archive-extract-launch snapshot-refusal cog-bindings cog-add-rebind invalid-archive light-rgb workbench thing-frames package native-inspect asset-notices overview bake-lighting terrain spiral terrain3do sectorarray dome swept statistics prefab thingpath dxf key cogcheck matbridge schema-inventory asset-create model-import model-import-normals model-lighting review-guards)
  add_test(NAME "sed_agent_${agent_scenario}"
    COMMAND sed_agent_tests "${agent_scenario}")
  set_tests_properties("sed_agent_${agent_scenario}" PROPERTIES
    LABELS "integration;agent"
    TIMEOUT 60
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endforeach()

add_test(NAME sed_agent_thing-append-archives COMMAND sed_agent_tests thing-append-archives)
add_test(NAME sed_agent_bake-light-limits COMMAND sed_agent_tests bake-light-limits)
set_tests_properties(sed_agent_bake-light-limits PROPERTIES
  LABELS "integration;agent;lighting" TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(sed_agent_thing-append-archives PROPERTIES
  LABELS "integration;agent;resources" TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(sed_agent_native_preservation_tests
  tests/native_preservation_tests.cpp
)
target_link_libraries(sed_agent_native_preservation_tests PRIVATE sed_agent_domain)
target_compile_features(sed_agent_native_preservation_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sed_agent_native_preservation_tests PRIVATE /FS)
endif()
add_test(NAME sed_agent_native_preservation COMMAND sed_agent_native_preservation_tests)
set_tests_properties(sed_agent_native_preservation PROPERTIES
  LABELS "integration;agent;preservation"
  TIMEOUT 60
)

add_executable(sed_agent_basic_operations_tests
  tests/basic_operations_tests.cpp
)
target_link_libraries(sed_agent_basic_operations_tests PRIVATE sed_agent_domain)
target_compile_features(sed_agent_basic_operations_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sed_agent_basic_operations_tests PRIVATE /FS)
endif()
add_test(NAME sed_agent_basic_operations COMMAND sed_agent_basic_operations_tests)
set_tests_properties(sed_agent_basic_operations PROPERTIES
  LABELS "integration;agent;geometry"
  TIMEOUT 60
)

add_executable(sed_agent_atomic_output_tests
  tests/atomic_output_tests.cpp
)
target_link_libraries(sed_agent_atomic_output_tests PRIVATE sed_agent_domain)
target_compile_features(sed_agent_atomic_output_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sed_agent_atomic_output_tests PRIVATE /FS)
endif()
add_test(NAME sed_agent_atomic_output COMMAND sed_agent_atomic_output_tests)
set_tests_properties(sed_agent_atomic_output PROPERTIES
  LABELS "integration;agent;files"
  TIMEOUT 60
)

foreach(contract IN ITEMS input_safety entity_operations query_details launch_plan schema_contract edit_operations arch_operations extended_query semantic_preview semantic_preview_model)
  add_executable(sed_agent_${contract}_tests
    tests/${contract}_tests.cpp
  )
  target_link_libraries(sed_agent_${contract}_tests PRIVATE sed_agent_domain)
  target_compile_features(sed_agent_${contract}_tests PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(sed_agent_${contract}_tests PRIVATE /FS)
  endif()
  add_test(NAME sed_agent_${contract} COMMAND sed_agent_${contract}_tests)
  set_tests_properties(sed_agent_${contract} PROPERTIES
    LABELS "integration;agent;contracts"
    TIMEOUT 60
  )
endforeach()

add_executable(sed_agent_json_contract_tests tests/json_contract_tests.cpp agent_json.cpp)
# agent_json parses numbers with sed::text::fromChars from sed_core.
target_link_libraries(sed_agent_json_contract_tests PRIVATE sed_core)
target_compile_features(sed_agent_json_contract_tests PRIVATE cxx_std_20)
add_test(NAME sed_agent_json_contract COMMAND sed_agent_json_contract_tests)
set_tests_properties(sed_agent_json_contract PROPERTIES LABELS "unit;agent;contracts" TIMEOUT 30)

add_executable(sed_agent_bake_resource_snapshot_tests tests/bake_resource_snapshot_tests.cpp)
target_link_libraries(sed_agent_bake_resource_snapshot_tests PRIVATE sed_core)
target_compile_features(sed_agent_bake_resource_snapshot_tests PRIVATE cxx_std_20)
add_test(NAME sed_agent_bake_resource_snapshot COMMAND sed_agent_bake_resource_snapshot_tests)
set_tests_properties(sed_agent_bake_resource_snapshot PROPERTIES LABELS "unit;agent;resources" TIMEOUT 60)

add_executable(sed_agent_archive_admission_tests tests/archive_admission_tests.cpp)
target_link_libraries(sed_agent_archive_admission_tests PRIVATE sed_core)
target_compile_features(sed_agent_archive_admission_tests PRIVATE cxx_std_20)
add_test(NAME sed_agent_archive_admission COMMAND sed_agent_archive_admission_tests)
set_tests_properties(sed_agent_archive_admission PROPERTIES LABELS "unit;agent;resources" TIMEOUT 30)
