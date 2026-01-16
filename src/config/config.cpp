/**
 * @file config.cpp
 * @brief Implementation of configuration parsing functions
 *
 * Parses JSON configuration files for OPC UA servers, OPC UA clients, and Modbus clients.
 * Handles file path resolution (tries both current directory and config/ subdirectory).
 */

#include "config.h"
#include "utils.h"
#include <csignal>

/**
 * @brief Reads OPC UA server configuration from a JSON file
 *
 * Parses the "servers" array from the JSON file and creates ServerConfig instances.
 * Tries to open the file in the current directory first, then in the config/ subdirectory.
 *
 * @param filename Name of the configuration file (e.g., "server_configuration.json")
 * @return Vector of ServerConfig structures (empty if file not found or parsing failed)
 */
std::vector<ServerConfig> readServerConfig(const std::string& filename) {
    std::vector<ServerConfig> serverConfigs;

    // Try to open the configuration file (first in current dir, then in config/ subdir)
    std::ifstream configFile;
    std::string used = filename;
    configFile.open(used);
    if (!configFile.is_open()) {
        used = "config/" + filename;
        configFile.open(used);
    }
    if (!configFile.is_open()) {
        std::cerr << "Could not open server config file: " << filename
            << " (also tried: config/" << filename << ")\n";
        return serverConfigs;
    }

    // Parse JSON content
    json config;
    try { configFile >> config; }
    catch (const std::exception& ex) {
        std::cerr << "Error parsing server config file (" << used << "): " << ex.what() << std::endl;
        return serverConfigs;
    }

    // Extract server configurations from the "servers" array
    if (config.contains("servers") && config["servers"].is_array()) {
        for (auto& srv : config["servers"]) {
            ServerConfig sc{};
            sc.port = srv.value("port", (UA_UInt16)4841);
            sc.plcAlias = srv.value("plcAlias", std::string{ "UnknownPLC" });
            sc.mode = srv.value("mode", std::string{ "CS" });  // Default: Client-Server mode
            sc.latencyTestEnabled = srv.value("latencyTestEnabled", false);
            serverConfigs.push_back(sc);
        }
    }
    else {
        std::cerr << "Server config file (" << used << ") does not contain a valid 'servers' array.\n";
    }
    return serverConfigs;
}

/**
 * @brief Reads OPC UA client configuration from a JSON file
 *
 * Parses the "plcs" array from the JSON file and creates PLCConfig instances.
 * Tries to open the file in the current directory first, then in the config/ subdirectory.
 *
 * @param filename Name of the configuration file (e.g., "client_configuration.json")
 * @return Vector of PLCConfig structures (empty if file not found or parsing failed)
 */
std::vector<PLCConfig> readClientConfig(const std::string& filename) {
    std::vector<PLCConfig> plcConfigs;

    // Try to open the configuration file (first in current dir, then in config/ subdir)
    std::ifstream configFile;
    std::string used = filename;
    configFile.open(used);
    if (!configFile.is_open()) {
        used = "config/" + filename;
        configFile.open(used);
    }
    if (!configFile.is_open()) {
        std::cerr << "Could not open client config file: " << filename
            << " (also tried: config/" << filename << ")\n";
        return plcConfigs;
    }

    // Parse JSON content
    json config;
    try { configFile >> config; }
    catch (const std::exception& ex) {
        std::cerr << "Error parsing client config file (" << used << "): " << ex.what() << std::endl;
        return plcConfigs;
    }

    // Extract PLC configurations from the "plcs" array
    if (config.contains("plcs") && config["plcs"].is_array()) {
        for (auto& plc : config["plcs"]) {
            // Skip PLCs without endpoint definition
            if (!plc.contains("endpoint")) continue;

            PLCConfig plcConfig{};
            plcConfig.endpoint = plc.value("endpoint", std::string{});
            plcConfig.endpointFallback = plc.value("endpointFallback", std::string{});
            plcConfig.alias = plc.value("alias", std::string{});
            plcConfig.pollIntervalSec = plc.value("pollIntervalSec", 5.0);

            // Parse nodes array for this PLC
            if (plc.contains("nodes") && plc["nodes"].is_array()) {
                for (auto& node : plc["nodes"]) {
                    // Skip nodes without nodeId
                    if (!node.contains("nodeId")) continue;
                    NodeConfig nc{};
                    nc.namespaceIndex = node.value("namespaceIndex", (UA_UInt16)0);
                    nc.nodeId = node["nodeId"].get<UA_UInt32>();
                    plcConfig.nodes.push_back(nc);
                }
            }

            // Parse optional method call configuration (for legacy servers requiring method invocation)
            if (plc.contains("methodCall") && plc["methodCall"].is_object()) {
                const auto& mc = plc["methodCall"];
                try {
                    plcConfig.methodNs = mc.value("namespaceIndex", (UA_UInt16)0);
                    plcConfig.methodObjectId = mc.value("objectId", (UA_UInt32)0);
                    plcConfig.methodId = mc.value("methodId", (UA_UInt32)0);
                    plcConfig.callBeforeEachPoll = mc.value("callBeforeEachPoll", true);
                    // Enable method call only if both object and method IDs are valid
                    plcConfig.methodCallEnabled = (plcConfig.methodObjectId != 0 && plcConfig.methodId != 0);
                }
                catch (...) {
                    plcConfig.methodCallEnabled = false;
                }
            }

            // Parse latency testing configuration
            plcConfig.latencyTestEnabled = plc.value("latencyTestEnabled", false);
            plcConfig.latencyCsvPath = plc.value("latencyCsvPath", std::string{});

            plcConfigs.push_back(plcConfig);
        }
    }
    else {
        std::cerr << "Client config file (" << used << ") does not contain a valid 'plcs' array.\n";
    }

    return plcConfigs;
}

/**
 * @brief Reads Modbus TCP client configuration from a JSON file
 *
 * Parses the "modbus" array from the JSON file and creates ModbusConfig instances.
 * Tries to open the file in the current directory first, then in the config/ subdirectory.
 *
 * @param filename Name of the configuration file (e.g., "client_configuration.json")
 * @return Vector of ModbusConfig structures (empty if file not found or parsing failed)
 */
std::vector<ModbusConfig> readModbusConfig(const std::string& filename) {
    std::vector<ModbusConfig> modbusConfigs;

    // Try to open the configuration file (first in current dir, then in config/ subdir)
    std::ifstream configFile;
    std::string used = filename;
    configFile.open(used);
    if (!configFile.is_open()) {
        used = "config/" + filename;
        configFile.open(used);
    }
    if (!configFile.is_open()) {
        std::cerr << "Could not open client config file: " << filename
            << " (also tried: config/" << filename << ")\n";
        return modbusConfigs;
    }

    // Parse JSON content
    json config;
    try { configFile >> config; }
    catch (const std::exception& ex) {
        std::cerr << "Error parsing client config file (" << used << "): " << ex.what() << std::endl;
        return modbusConfigs;
    }

    // Extract Modbus configurations from the "modbus" array
    if (config.contains("modbus") && config["modbus"].is_array()) {
        for (auto& mb : config["modbus"]) {
            ModbusConfig mc{};
            mc.ip = mb.value("ip", std::string{});
            mc.port = mb.value("port", 502);  // Default Modbus TCP port
            mc.alias = mb.value("alias", std::string{ "Modbus" });
            mc.bindToPlc = mb.value("bindToPlc", std::string{});
            mc.pollIntervalSec = mb.value("pollIntervalSec", 1.0);  // Default 1 second polling

            // Parse register definitions for this Modbus device
            if (mb.contains("registers") && mb["registers"].is_array()) {
                for (auto& reg : mb["registers"]) {
                    ModbusRegisterConfig rc{};
                    rc.name = reg.value("name", std::string{ "Register" });
                    rc.address = reg.value("address", 0);
                    rc.datatype = reg.value("datatype", std::string{ "uint16" });
                    mc.registers.push_back(rc);
                }
            }
            modbusConfigs.push_back(mc);
        }
    }
    else {
        std::cerr << "Client config file (" << used << ") does not contain a valid 'modbus' array.\n";
    }

    return modbusConfigs;
}