/**
 * @file utils.h
 * @brief Utility functions for key generation and path management
 *
 * Provides helper functions for creating consistent data keys, extracting PLC aliases
 * from endpoint URLs, and managing namespace metadata file paths.
 */

#pragma once
#include <string>
#include <filesystem>

/**
 * @brief Extracts a PLC alias from an OPC UA endpoint URL
 *
 * Parses the endpoint URL to extract the IP address and generates a PLC alias
 *
 * @param endpoint The OPC UA endpoint URL (e.g., "opc.tcp://192.168.1.100:4840")
 * @return PLC alias string (e.g., "PLC100"), or "PLC_Unknown" if parsing fails
 */
std::string getAliasFromEndpoint(const std::string& endpoint);

/**
 * @brief Generates a unique data key for a node
 *
 * Creates a standardized key string for storing and retrieving node data in the
 * global data maps. Format: "alias:nsIndex:nodeId"
 *
 * @param alias The PLC alias identifier
 * @param nsIndex The OPC UA namespace index
 * @param nodeId The numeric node identifier
 * @return Formatted key string for use in ThreadSafeMap
 */
std::string getDataKey(const std::string& alias, uint16_t nsIndex, uint32_t nodeId);

/**
 * @brief Generates a file path for storing namespace metadata
 *
 * Creates a path to a JSON file that stores namespace metadata for a specific PLC
 * and namespace index. Creates the directory structure if it doesn't exist.
 *
 * @param alias The PLC alias identifier
 * @param nsIndex The OPC UA namespace index
 * @return File system path to the namespace JSON file
 */
std::filesystem::path getNamespaceJsonPath(const std::string& alias, uint16_t nsIndex);