cmake_minimum_required(VERSION 3.10)
project(aggregation_server)

set(CMAKE_CXX_STANDARD 17)

# ========== Build open62541 with all required features ==========
message(STATUS "Configuring open62541 v1.4.8 with encryption and all features...")

# Open62541 Options
set(UA_ENABLE_ENCRYPTION OPENSSL CACHE STRING "Enable encryption with OpenSSL")
set(UA_ENABLE_PUBSUB ON CACHE BOOL "Enable PubSub protocol")
set(UA_ENABLE_PUBSUB_INFORMATIONMODEL ON CACHE BOOL "Enable PubSub information model")
set(UA_ENABLE_DA ON CACHE BOOL "Enable DataAccess")
set(UA_ENABLE_SUBSCRIPTIONS ON CACHE BOOL "Enable subscriptions")
set(UA_ENABLE_SUBSCRIPTIONS_EVENTS ON CACHE BOOL "Enable event monitoring")
set(UA_ENABLE_METHODCALLS ON CACHE BOOL "Enable Method service set")
set(UA_ENABLE_NODEMANAGEMENT ON CACHE BOOL "Enable dynamic node management")
set(UA_ENABLE_DISCOVERY ON CACHE BOOL "Enable Discovery Service")
set(UA_ENABLE_DISCOVERY_SEMAPHORE ON CACHE BOOL "Enable Discovery Semaphore")
set(UA_ENABLE_HISTORIZING ON CACHE BOOL "Enable historical access")
set(UA_ENABLE_JSON_ENCODING ON CACHE BOOL "Enable JSON encoding")
set(UA_ENABLE_DIAGNOSTICS ON CACHE BOOL "Enable diagnostics")
set(UA_ENABLE_STATUSCODE_DESCRIPTIONS ON CACHE BOOL "Enable StatusCode descriptions")
set(UA_ENABLE_TYPEDESCRIPTION ON CACHE BOOL "Enable type descriptions")
set(UA_NAMESPACE_ZERO REDUCED CACHE STRING "Namespace zero completeness")
set(UA_MULTITHREADING 100 CACHE STRING "Thread-safety level")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static library")
set(UA_BUILD_EXAMPLES OFF CACHE BOOL "Don't build examples")
set(UA_BUILD_UNIT_TESTS OFF CACHE BOOL "Don't build unit tests")

# Add open62541 subdirectory
add_subdirectory(external/open62541)

# Add nlohmann/json (header-only library)
set(JSON_Install OFF CACHE INTERNAL "")
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(external/json)

# ========== AggregationServer Configuration ==========

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/server)
include_directories(${CMAKE_SOURCE_DIR}/src/client)
include_directories(${CMAKE_SOURCE_DIR}/src/config)
include_directories(${CMAKE_SOURCE_DIR}/src/utils)
include_directories(${CMAKE_SOURCE_DIR}/src/thread)
include_directories(${CMAKE_SOURCE_DIR}/external/open62541/include)
include_directories(${CMAKE_BINARY_DIR}/external/open62541/src_generated)
include_directories(/usr/include/modbus)

# Source files
add_executable(aggregation_server
    src/main.cpp
    src/server/server.cpp
    src/client/client.cpp
    src/utils/utils.cpp
    src/SharedData.cpp
    src/config/config.cpp
)

# Link libraries
target_link_libraries(aggregation_server
    open62541
    nlohmann_json::nlohmann_json
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(aggregation_server
        ws2_32
        iphlpapi
        crypt32
    )
else()
    target_link_libraries(aggregation_server
        modbus
        pthread
        ssl
        crypto
    )
endif()

message(STATUS "=================================================")
message(STATUS "AggregationServer Configuration Complete")
message(STATUS "  - open62541 v1.4.8 with OpenSSL encryption")
message(STATUS "  - PubSub: ON")
message(STATUS "  - All required features: ENABLED")
message(STATUS "=================================================")
