cmake_minimum_required(VERSION 3.16)
project(aeronWrapper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Fetch Aeron from GitHub
FetchContent_Declare(
    aeron
    GIT_REPOSITORY https://github.com/aeron-io/aeron.git
    GIT_TAG        master  # or specify a specific version tag like "1.45.0"
)

# Configure Aeron build options
set(BUILD_AERON_DRIVER ON CACHE BOOL "Build Aeron driver")
set(BUILD_AERON_ARCHIVE_API ON CACHE BOOL "Build Aeron archive API")
set(C_WARNINGS_AS_ERRORS OFF CACHE BOOL "Disable warnings as errors")
set(CXX_WARNINGS_AS_ERRORS OFF CACHE BOOL "Disable warnings as errors")

FetchContent_MakeAvailable(aeron)

# Collect source files
file(GLOB SOURCES "src/*.cpp")
# Collect header files
file(GLOB HEADERS "src/*.h")

add_library(${PROJECT_NAME} ${SOURCES})

# Include directories
# - Expose wrapper headers to consumers
# - Expose Aeron headers to consumers (build and install interfaces)
# - Keep Aeron driver C headers private for build
if (DEFINED aeron_SOURCE_DIR)
    set(AERON_CLIENT_INCLUDE_DIR ${aeron_SOURCE_DIR}/aeron-client/src/main/cpp)
    set(AERON_DRIVER_C_INCLUDE_DIR ${aeron_SOURCE_DIR}/aeron-driver/src/main/c)
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${AERON_CLIENT_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include/aeron>
    PRIVATE
        ${AERON_DRIVER_C_INCLUDE_DIR}
)

# No PUBLIC/INTERFACE link to Aeron here; linkage is injected via Config file at find_package time

# Installation
install(TARGETS ${PROJECT_NAME}
        EXPORT aeronWrapperTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install wrapper public headers
install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/)

# Also install Aeron client headers so downstreams don't need a separate Aeron install
if (EXISTS ${AERON_CLIENT_INCLUDE_DIR})
    install(DIRECTORY ${AERON_CLIENT_INCLUDE_DIR}/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aeron
            FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
    )
endif()

# Install Aeron client library that was built via FetchContent
# (Do not export it; consumers will get full-path linkage from the Config file)
if (TARGET aeron_client)
    install(TARGETS aeron_client
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# Export targets and generate package config
install(EXPORT aeronWrapperTargets
        NAMESPACE aeronWrapper::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aeronWrapper
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/aeronWrapperConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/aeronWrapperConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/aeronWrapperConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aeronWrapper
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/aeronWrapperConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/aeronWrapperConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aeronWrapper
)
