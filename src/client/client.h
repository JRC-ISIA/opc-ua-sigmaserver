/**
 * @file client.h
 * @brief OPC UA and Modbus client interfaces for data acquisition
 *
 * Provides functions for connecting to OPC UA PLCs and Modbus TCP devices,
 * discovering node structures, reading values, and storing data in global maps.
 */

#pragma once

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <string>
#include <vector>
#include "config.h"
#include <nlohmann/json.hpp>

/**
 * @brief Loads and stores structural metadata for an OPC UA node
 *
 * Retrieves node metadata including DisplayName, Description, DataType, BrowseName,
 * and NamespaceURI from the PLC. Stores this information in g_structureMap for use
 * by the server during node creation. Additionally maintains a JSON file per namespace
 * (config/<PLC_Alias>/NamespaceX.json) containing a list of all discovered nodes.
 *
 * @param client Connected OPC UA client instance
 * @param nodeConfig Node configuration (namespace index and node ID)
 * @param plcFolder PLC alias used as folder name for storing metadata
 */
void loadNodeStructure(UA_Client* client, const NodeConfig& nodeConfig, const std::string& plcFolder);

/**
 * @brief Reads and stores the current value of an OPC UA node
 *
 * Reads the current value from the PLC node and stores it in g_dataMap as JSON.
 * Supports all common OPC UA data types (numeric, string, boolean, datetime, etc.).
 * Tracks t1 latency if latency testing is enabled.
 *
 * @param client Connected OPC UA client instance
 * @param nodeConfig Node configuration (namespace index and node ID)
 * @param plcFolder PLC alias for key generation
 */
void updateNodeValue(UA_Client* client, const NodeConfig& nodeConfig, const std::string& plcFolder);

/**
 * @brief Runs an OPC UA client for a specific PLC endpoint
 *
 * Main client loop that connects to a PLC, loads node structures, and continuously
 * polls for value updates. Handles connection failures with fallback endpoints and
 * optional method calls for legacy servers.
 *
 * @param plcConfig Complete PLC configuration including endpoint, nodes, and polling interval
 */
void runPLCClientForEndpoint(const PLCConfig& plcConfig);

/**
 * @brief Starts OPC UA clients for all PLCs defined in a configuration file
 *
 * Reads the client configuration file and starts a separate client thread for each
 * configured PLC endpoint.
 *
 * @param clientConfigFile Path to the client configuration JSON file
 */
void runPLCClientConfigurable(const std::string& clientConfigFile);

/**
 * @brief Runs a Modbus TCP client for a configured Modbus device
 *
 * Connects to a Modbus TCP server and continuously polls configured registers,
 * storing values in g_dataMap. Supports multiple data types (uint16, int16, float, etc.).
 *
 * @param modbusConfig Complete Modbus configuration including IP, port, registers, and polling interval
 */
void runModbusClient(const ModbusConfig& modbusConfig);

/**
 * @brief Invokes a remote method on the OPC UA server
 *
 * Calls a configured method on the PLC server. Used for legacy servers that require
 * method invocation before reading values (e.g., to trigger data updates).
 *
 * @param client Connected OPC UA client instance
 * @param plcConfig PLC configuration containing method call settings
 */
void callRemoteMethod(UA_Client* client, const PLCConfig& plcConfig);