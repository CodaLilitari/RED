# Keep complete notice lists out of the build shell's command line. Windows
# cmd.exe truncates commands beyond 8191 characters even inside a batch file.
function(_sed_package_setting OUTPUT NAME VALUE)
  set(_equals "=")
  string(FIND "${VALUE}" "]${_equals}]" _closing)
  while(NOT _closing EQUAL -1)
    string(APPEND _equals "=")
    string(FIND "${VALUE}" "]${_equals}]" _closing)
  endwhile()
  set(${OUTPUT} "${${OUTPUT}}set(${NAME} [${_equals}[${VALUE}]${_equals}])\n" PARENT_SCOPE)
endfunction()

# The generated file is per configuration, preserving target/config generator
# expressions without changing the release script's explicit input contract.
function(sed_add_release_package_target)
  set(_script "")
  _sed_package_setting(_script SED_RELEASE_STAGE_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/stage_release_package.cmake")
  _sed_package_setting(_script SED_PACKAGE_EXECUTABLE "$<TARGET_FILE:sed_app>")
  _sed_package_setting(_script SED_PACKAGE_OUTPUT_DIR "${SED_RELEASE_PACKAGE_OUTPUT_DIR}")
  _sed_package_setting(_script SED_PACKAGE_DATA_DIR "${CMAKE_SOURCE_DIR}/data")
  _sed_package_setting(_script SED_PACKAGE_RUNTIME_DATA "${SED_RUNTIME_DATA_SERIALIZED}")
  set(_dependencies sed_app)
  if(SED_BUILD_AGENT)
    _sed_package_setting(_script SED_PACKAGE_AGENT_EXECUTABLE "$<TARGET_FILE:sed_agent>")
    list(APPEND _dependencies sed_agent)
  endif()
  if(SED_BUILD_MCP)
    _sed_package_setting(_script SED_PACKAGE_MCP_EXECUTABLE "$<TARGET_FILE:sed_mcp>")
    list(APPEND _dependencies sed_mcp)
  endif()
  _sed_package_setting(_script SED_PACKAGE_CAPTURE_RUNNER "${CMAKE_SOURCE_DIR}/tools/engine-capture/invoke_engine_capture.py")
  _sed_package_setting(_script SED_PACKAGE_CAPTURE_README "${CMAKE_SOURCE_DIR}/tools/engine-capture/README.md")
  _sed_package_setting(_script SED_PACKAGE_CAPTURE_GAMEPLAY "${CMAKE_SOURCE_DIR}/tools/engine-capture/gameplay_capture.py")
  _sed_package_setting(_script SED_PACKAGE_PROJECT_LICENSE_FILES "${SED_RELEASE_PACKAGE_PROJECT_LICENSE_FILES}")
  _sed_package_setting(_script SED_PACKAGE_VERSION "${PROJECT_VERSION}")
  _sed_package_setting(_script SED_PACKAGE_SOURCE_COMMIT "${SED_RELEASE_SOURCE_COMMIT}")
  _sed_package_setting(_script SED_PACKAGE_PLATFORM "Windows")
  _sed_package_setting(_script SED_PACKAGE_MSVC "$<BOOL:${MSVC}>")
  _sed_package_setting(_script SED_PACKAGE_CONFIGURATION "$<CONFIG>")
  _sed_package_setting(_script SED_RELEASE_QT_REQUIRED_VERSION "${SED_RELEASE_QT_REQUIRED_VERSION}")
  _sed_package_setting(_script SED_RELEASE_QT_VERSION "${SED_RELEASE_QT_VERSION}")
  _sed_package_setting(_script SED_RELEASE_QT_VERSION_ROOT "${_sed_release_qt_version_root}")
  _sed_package_setting(_script SED_RELEASE_QT_KIT_ROOT "${_sed_release_qt_kit_root}")
  _sed_package_setting(_script SED_RELEASE_QT_DEPLOY_TOOL "${_sed_release_qt_deploy_tool}")
  _sed_package_setting(_script SED_RELEASE_QT_LICENSE_FILES "${SED_RELEASE_QT_LICENSE_FILES}")
  _sed_package_setting(_script SED_RELEASE_QT_SBOM_FILES "${SED_RELEASE_QT_SBOM_FILES}")
  _sed_package_setting(_script SED_RELEASE_MSVC_VS_ROOT "${_sed_release_msvc_vs_root}")
  _sed_package_setting(_script SED_RELEASE_MSVC_NOTICE_FILES "${SED_RELEASE_MSVC_NOTICE_FILES}")
  _sed_package_setting(_script SED_RELEASE_MSVC_RUNTIME_FILES "${_sed_release_msvc_runtime_files}")
  _sed_package_setting(_script _SED_RELEASE_ENTRY_POINT
    "${CMAKE_CURRENT_SOURCE_DIR}/release_package.cmake")
  string(APPEND _script "include(\"\${_SED_RELEASE_ENTRY_POINT}\")\n")
  set(_settings "${CMAKE_CURRENT_BINARY_DIR}/release-package-$<CONFIG>.cmake")
  file(GENERATE OUTPUT "${_settings}" CONTENT "${_script}")
  add_custom_target(sed_release_package
    COMMAND "${CMAKE_COMMAND}" -P "${_settings}"
    DEPENDS ${_dependencies}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Staging the explicit SED Release package"
    VERBATIM
  )
endfunction()
