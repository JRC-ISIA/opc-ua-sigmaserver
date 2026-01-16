/**
 * @file utils.cpp
 * @brief Implementation of utility functions
 */

#include "utils.h"
#include <regex>

/**
 * @brief Extracts a PLC alias from an OPC UA endpoint URL
 *
 * Uses regex to parse the IP address from the endpoint URL and generates
 * a PLC alias using the last octet. This provides a simple but effective
 * way to create human-readable identifiers for PLCs based on their IP addresses.
 *
 * @param endpoint The OPC UA endpoint URL
 * @return PLC alias (e.g., "PLC100") or "PLC_Unknown" if parsing fails
 */
std::string getAliasFromEndpoint(const std::string& endpoint) {
    // Regex pattern to match OPC UA TCP endpoint format
    std::regex ipRegex(R"(opc\.tcp://([\d\.]+):\d+)");
    std::smatch match;

    // Extract IP address from endpoint URL
    if (std::regex_search(endpoint, match, ipRegex) && match.size() > 1) {
        std::string ip = match[1].str();
        size_t lastDot = ip.rfind('.');

        // Use last octet of IP as PLC identifier
        if (lastDot != std::string::npos) {
            return "PLC" + ip.substr(lastDot + 1);
        }
    }

    // Fallback if parsing fails
    return "PLC_Unknown";
}

/**
 * @brief Generates a unique data key for a node
 *
 * Creates a composite key by concatenating the PLC alias, namespace index,
 * and node ID with colons as separators. This format is used throughout
 * the application for consistent key generation.
 *
 * @param alias The PLC alias identifier
 * @param nsIndex The OPC UA namespace index
 * @param nodeId The numeric node identifier
 * @return Formatted key string (format: "alias:nsIndex:nodeId")
 */
std::string getDataKey(const std::string& alias, uint16_t nsIndex, uint32_t nodeId) {
    return alias + ":" + std::to_string(nsIndex) + ":" + std::to_string(nodeId);
}

/**
 * @brief Generates a file path for storing namespace metadata
 *
 * Creates a structured path within the config directory for storing namespace
 * metadata as JSON files. Each PLC gets its own subdirectory, and each namespace
 * gets a separate JSON file.
 *
 * @param alias The PLC alias identifier
 * @param nsIndex The OPC UA namespace index
 * @return Complete file system path to the namespace JSON file
 */
std::filesystem::path getNamespaceJsonPath(const std::string& alias, uint16_t nsIndex) {
    std::filesystem::path baseDir("config");
    std::filesystem::path plcDir = baseDir / alias;

    // Create directory structure if it doesn't exist
    std::filesystem::create_directories(plcDir);

    // Return full path to namespace JSON file
    return plcDir / ("Namespace" + std::to_string(nsIndex) + ".json");
}
