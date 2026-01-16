/**
 * @file config.h
 * @brief Configuration structures and JSON parsing functions
 *
 * Defines data structures for OPC UA clients, Modbus clients, and OPC UA servers,
 * along with functions to parse configuration from JSON files.
 */

#pragma once
#include <open62541/server_config_default.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

/**
 * @brief Configuration for a single OPC UA node to monitor
 */
struct NodeConfig {
    UA_UInt16 namespaceIndex;  ///< OPC UA namespace index of the node
    UA_UInt32 nodeId;          ///< Numeric node identifier within the namespace
};

/**
 * @brief Configuration for a single Modbus register
 */
struct ModbusRegisterConfig {
    std::string name;       ///< Logical name for the register (used as key in data map)
    int         address;    ///< Modbus register address (holding register)
    std::string datatype;   ///< Data type: "uint16", "int16", "float", "uint32", etc.
};

/**
 * @brief Configuration for an OPC UA PLC client connection
 *
 * Defines connection parameters, nodes to monitor, polling behavior,
 * optional method calls, and latency testing settings.
 */
struct PLCConfig {
    std::string endpoint;             ///< Primary OPC UA endpoint URL (e.g., "opc.tcp://192.168.1.100:4840")
    std::string endpointFallback;     ///< Fallback endpoint URL if primary fails
    std::string alias;                ///< Unique alias for this PLC (used as key prefix in data maps)
    std::vector<NodeConfig> nodes;    ///< List of nodes to monitor from this PLC
    double      pollIntervalSec;      ///< Polling interval in seconds for reading node values

    // Optional method call configuration for legacy servers
    bool        methodCallEnabled = false;     ///< Enable method invocation before reading
    UA_UInt16   methodNs = 0;                 ///< Namespace index of the method object
    UA_UInt32   methodObjectId = 0;           ///< Node ID of the object containing the method
    UA_UInt32   methodId = 0;                 ///< Node ID of the method to call
    bool        callBeforeEachPoll = true;    ///< If true, call method before each poll cycle

    // Latency testing configuration
    bool        latencyTestEnabled = false;   ///< Enable latency measurement for this PLC
    std::string latencyCsvPath = "";          ///< CSV file path for exporting latency statistics
};

/**
 * @brief Configuration for an OPC UA server instance
 *
 * Defines the server port, which PLC data to expose, operational mode,
 * and latency testing settings.
 */
struct ServerConfig {
    UA_UInt16       port;                     ///< TCP port for the OPC UA server (e.g., 4840)
    std::string     plcAlias;                 ///< PLC alias whose data this server exposes
    std::string     mode;                     ///< Server mode: "CS" (Client-Server) or "PubSub"
    bool            latencyTestEnabled = false; ///< Enable latency measurement (t2 timing)
};

/**
 * @brief Configuration for a Modbus TCP client connection
 *
 * Defines connection parameters, register mappings, and polling behavior
 * for a Modbus device.
 */
struct ModbusConfig {
    std::string ip;                                ///< IP address of the Modbus TCP server
    int         port;                              ///< TCP port (typically 502)
    std::string alias;                             ///< Unique alias for this Modbus device
    std::string bindToPlc;                         ///< PLC alias to associate with (for namespace mapping)
    std::vector<ModbusRegisterConfig> registers;   ///< List of registers to read from the device
    double      pollIntervalSec;                   ///< Polling interval in seconds
};

/// JSON library alias for convenience
using json = nlohmann::json;

/**
 * @brief Reads server configuration from a JSON file
 * @param filename Path to the server configuration JSON file (e.g., "server_configuration.json")
 * @return Vector of ServerConfig structures, one per configured server
 */
std::vector<ServerConfig> readServerConfig(const std::string& filename);

/**
 * @brief Reads OPC UA client configuration from a JSON file
 * @param filename Path to the client configuration JSON file (e.g., "client_configuration.json")
 * @return Vector of PLCConfig structures, one per configured PLC
 */
std::vector<PLCConfig> readClientConfig(const std::string& filename);

/**
 * @brief Reads Modbus client configuration from a JSON file
 * @param filename Path to the client configuration JSON file (same as for OPC UA clients)
 * @return Vector of ModbusConfig structures, one per configured Modbus device
 */
std::vector<ModbusConfig> readModbusConfig(const std::string& filename);