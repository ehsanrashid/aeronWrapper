#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "aeronWrapper::aeronWrapper" for configuration "Release"
set_property(TARGET aeronWrapper::aeronWrapper APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(aeronWrapper::aeronWrapper PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libaeronWrapper.a"
  )

list(APPEND _cmake_import_check_targets aeronWrapper::aeronWrapper )
list(APPEND _cmake_import_check_files_for_aeronWrapper::aeronWrapper "${_IMPORT_PREFIX}/lib/libaeronWrapper.a" )

# Import target "aeronWrapper::aeron_client" for configuration "Release"
set_property(TARGET aeronWrapper::aeron_client APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(aeronWrapper::aeron_client PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libaeron_client.a"
  )

list(APPEND _cmake_import_check_targets aeronWrapper::aeron_client )
list(APPEND _cmake_import_check_files_for_aeronWrapper::aeron_client "${_IMPORT_PREFIX}/lib/libaeron_client.a" )

# Import target "aeronWrapper::aeron_driver" for configuration "Release"
set_property(TARGET aeronWrapper::aeron_driver APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(aeronWrapper::aeron_driver PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libaeron_driver.so"
  IMPORTED_SONAME_RELEASE "libaeron_driver.so"
  )

list(APPEND _cmake_import_check_targets aeronWrapper::aeron_driver )
list(APPEND _cmake_import_check_files_for_aeronWrapper::aeron_driver "${_IMPORT_PREFIX}/lib/libaeron_driver.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
