
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was aeronWrapperConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include("${CMAKE_CURRENT_LIST_DIR}/aeronWrapperTargets.cmake")

# Locate Aeron client library installed with this package
set(_aeronwrapper_lib_dir "${PACKAGE_PREFIX_DIR}/lib")
find_library(AERONWRAPPER_AERON_CLIENT_LIB
    NAMES aeron_client
    PATHS "${_aeronwrapper_lib_dir}"
    NO_DEFAULT_PATH
)

if (NOT AERONWRAPPER_AERON_CLIENT_LIB)
    message(FATAL_ERROR "aeron_client library not found in ${_aeronwrapper_lib_dir}")
endif()

# Choose imported library type based on filename extension
get_filename_component(_aeron_client_ext "${AERONWRAPPER_AERON_CLIENT_LIB}" EXT)
if (_aeron_client_ext STREQUAL ".a")
    set(_aeron_client_import_type STATIC)
else()
    # Covers .so, .dylib, versioned .so.N
    set(_aeron_client_import_type SHARED)
endif()

# Define imported target for Aeron client and link it publicly
add_library(aeronWrapper::aeron_client ${_aeron_client_import_type} IMPORTED GLOBAL)
set_target_properties(aeronWrapper::aeron_client PROPERTIES
    IMPORTED_LOCATION "${AERONWRAPPER_AERON_CLIENT_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${PACKAGE_PREFIX_DIR}/include;${PACKAGE_PREFIX_DIR}/include/aeron"
)

target_link_libraries(aeronWrapper::aeronWrapper INTERFACE aeronWrapper::aeron_client)

check_required_components(aeronWrapper) 
