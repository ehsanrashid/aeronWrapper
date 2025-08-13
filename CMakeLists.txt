cmake_minimum_required(VERSION 3.16)
project(aeronWrapper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Collect source files
file(GLOB SOURCE_FILES "*.cpp")

file(GLOB HEADERS "*.h")

add_library(${PROJECT_NAME} ${SOURCE_FILES})

# Include Aeron headers
target_include_directories(${PROJECT_NAME}  PRIVATE 
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
