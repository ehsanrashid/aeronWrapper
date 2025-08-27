cmake_minimum_required(VERSION 3.16)
project(aeronWrapper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

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
file(GLOB SOURCE_FILES "*.cpp")
# Collect header files
file(GLOB HEADERS "*.h")

add_library(${PROJECT_NAME} ${SOURCE_FILES})

# Include Aeron headers
target_include_directories(${PROJECT_NAME}  PUBLIC 
    ${aeron_SOURCE_DIR}/aeron-client/src/main/cpp
    ${aeron_SOURCE_DIR}/aeron-driver/src/main/c
)

# Link against Aeron libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
    aeron_driver
    aeron_client
)

install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include)
install(FILES ${HEADERS} DESTINATION include/)
