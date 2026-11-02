add_executable(sed_cpp_bridge
  main.cpp
)
set_target_properties(sed_cpp_bridge PROPERTIES OUTPUT_NAME "sed-cpp-bridge")
target_link_libraries(sed_cpp_bridge PRIVATE sed_core)
target_compile_features(sed_cpp_bridge PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sed_cpp_bridge PRIVATE /FS)
endif()
