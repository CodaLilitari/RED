# Out-of-process JED plug-in bridge. Links sed_core only (no Qt) so it can be
# built for a different architecture than RED, e.g. a 32-bit Windows bridge for
# legacy 32-bit JED plug-ins under 64-bit RED. RED looks for
# red-jed-plugin-bridge-<arch>[.exe] beside its own executable.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(SED_PLUGIN_BRIDGE_ARCH x86)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$"
    OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
  set(SED_PLUGIN_BRIDGE_ARCH arm64)
else()
  set(SED_PLUGIN_BRIDGE_ARCH x64)
endif()

add_executable(red_jed_plugin_bridge main.cpp)
target_link_libraries(red_jed_plugin_bridge PRIVATE sed_core ${CMAKE_DL_LIBS})
target_compile_features(red_jed_plugin_bridge PRIVATE cxx_std_20)
set_target_properties(red_jed_plugin_bridge PROPERTIES
  OUTPUT_NAME "red-jed-plugin-bridge-${SED_PLUGIN_BRIDGE_ARCH}")
if(MSVC)
  target_compile_options(red_jed_plugin_bridge PRIVATE /FS)
endif()
