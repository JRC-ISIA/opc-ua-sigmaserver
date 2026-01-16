/**
 * @file server.cpp
 * @brief OPC UA server implementation for exposing aggregated data
 *
 * Implements an OPC UA server that dynamically creates nodes based on discovered
 * PLC structures and exposes real-time data from global data maps. Supports:
 * - Dynamic node creation from structure metadata
 * - Continuous value updates via read callbacks
 * - Security with Basic256Sha256 and certificate authentication
 * - Both Client-Server and PubSub modes
 * - Latency tracking (t2 measurement)
 */

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "server.h"
#include "SharedData.h"
#include "config.h"
#include "StoppableThread.h"
#include "LatencyTracking.h"
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/accesscontrol_default.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstring>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <cstring>
#include <open62541/server_pubsub.h>

 /**
  * @brief Loads a certificate or private key file from multiple possible locations
  *
  * @param path Base filename of the certificate/key file (e.g., "server_cert.der")
  * @return UA_ByteString containing the file contents, or UA_STRING_NULL on failure
  */
static UA_ByteString loadCertificateFile(const char* path) {
    UA_ByteString fileContents = UA_STRING_NULL;

    // Try multiple possible paths
    std::vector<std::string> paths = {
        path,
        std::string("certs/") + path,
        std::string("certs/server/") + path,  // New organized structure
        std::string("../certs/") + path,
        std::string("../certs/server/") + path,  // New organized structure
        std::string("./") + path
    };

    FILE* fp = nullptr;
    std::string usedPath;

    for (const auto& tryPath : paths) {
        fp = fopen(tryPath.c_str(), "rb");
        if (fp) {
            usedPath = tryPath;
            break;
        }
    }

    if (!fp) {
        std::cerr << "[Security] Could not open certificate file: " << path << std::endl;
        std::cerr << "[Security] Tried paths: ";
        for (const auto& p : paths) std::cerr << p << " ";
        std::cerr << std::endl;
        return fileContents;
    }

    fseek(fp, 0, SEEK_END);
    fileContents.length = (size_t)ftell(fp);
    fileContents.data = (UA_Byte*)UA_malloc(fileContents.length);

    if (!fileContents.data) {
        std::cerr << "[Security] Memory allocation failed for certificate" << std::endl;
        fclose(fp);
        return UA_STRING_NULL;
    }

    fseek(fp, 0, SEEK_SET);
    size_t read = fread(fileContents.data, 1, fileContents.length, fp);
    fclose(fp);

    if (read != fileContents.length) {
        std::cerr << "[Security] Failed to read complete certificate file" << std::endl;
        UA_ByteString_clear(&fileContents);
        return UA_STRING_NULL;
    }

    std::cout << "[Security] Successfully loaded: " << usedPath
        << " (" << fileContents.length << " bytes)" << std::endl;

    return fileContents;
}

/**
 * @brief Loads a certificate file (wrapper for loadCertificateFile)
 * @param certPath Path to the certificate file
 * @return UA_ByteString containing the certificate data
 */
static UA_ByteString loadCertificate(const char* certPath) {
    return loadCertificateFile(certPath);
}

/**
 * @brief Loads a private key file (wrapper for loadCertificateFile)
 * @param keyPath Path to the private key file
 * @return UA_ByteString containing the private key data
 */
static UA_ByteString loadPrivateKey(const char* keyPath) {
    return loadCertificateFile(keyPath);
}

/**
 * @brief Overrides the default OPC UA application URI with a PLC-specific URI
 *
 * Replaces the default "urn:open62541.unconfigured.application" URI with the
 * namespace URI from the PLC, ensuring proper namespace identification in the
 * server's application description.
 *
 * @param server Pointer to the OPC UA server instance
 * @param plcNamespaceUri The desired namespace URI from the PLC
 */
void overrideDefaultApplicationUri(UA_Server* server, const std::string& plcNamespaceUri) {
    UA_ServerConfig* config = UA_Server_getConfig(server);
    UA_String_clear(&config->applicationDescription.applicationUri);
    config->applicationDescription.applicationUri = UA_STRING_ALLOC(plcNamespaceUri.c_str());

    std::cout << "Overridden applicationUri with PLC URI: " << plcNamespaceUri << std::endl;
}

/**
 * @brief Read callback that provides dynamic node values from g_dataMap
 *
 * This callback is invoked whenever an OPC UA client reads a node value.
 * It retrieves the current value from g_dataMap and converts it from JSON
 * to the appropriate OPC UA data type based on the node's data type metadata.
 *
 * @param server Pointer to the OPC UA server instance
 * @param sessionId Session identifier of the reading client
 * @param sessionContext Session-specific context data
 * @param nodeId Node identifier being read
 * @param nodeContext Node-specific context (contains data map key as char*)
 * @param range Numeric range for array access (unused)
 * @param value Output parameter that receives the node value
 */
static void dynamicValueReadCallback(UA_Server* server,
    const UA_NodeId* sessionId,
    void* sessionContext,
    const UA_NodeId* nodeId,
    void* nodeContext,
    const UA_NumericRange* range,
    const UA_DataValue* value)
{
    // Validate node context contains the data map key
    if (!nodeContext) return;
    const char* key = static_cast<const char*>(nodeContext);

    // Start t2 latency measurement: server begins value retrieval
    g_latencyTracker.startT2(key);

    // Retrieve current value from global data map
    nlohmann::json jVal;
    if (!g_dataMap.get(key, jVal)) {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            "No dynamic value for key %s", key);
        g_latencyTracker.stopT2(key);  // Stop timing even on error
        return;
    }

    // Determine OPC UA data type from structure metadata (default: STRING)
    UA_UInt32 dtId = UA_NS0ID_STRING;
    nlohmann::json jStruc;
    if (g_structureMap.get(key, jStruc)) {
        try {
            if (jStruc.contains("DataType")) {
                dtId = (UA_UInt32)std::stoul(jStruc["DataType"].get<std::string>());
            }
        }
        catch (...) { /* Fallback to STRING on parsing error */ }
    }

    // Prepare output data value with timestamp
    UA_DataValue* out = const_cast<UA_DataValue*>(value);
    out->hasValue = true;
    out->sourceTimestamp = UA_DateTime_now();

    // Helper lambda for setting numeric values
    auto setNum = [&](auto v, const UA_DataType* t) {
        UA_Variant_setScalarCopy(&out->value, &v, t);
        };

    // Convert JSON value to appropriate OPC UA variant type
    try {
        if (!jVal.contains("Value") || jVal["Value"].is_null()) {
            UA_Variant_clear(&out->value);
            out->hasValue = false;
            return;
        }

        switch (dtId) {
        case UA_NS0ID_BOOLEAN: {
            UA_Boolean v = jVal["Value"].is_boolean()
                ? (UA_Boolean)jVal["Value"].get<bool>()
                : (UA_Boolean)(jVal["Value"].get<int64_t>() != 0);
            setNum(v, &UA_TYPES[UA_TYPES_BOOLEAN]); break;
        }
        case UA_NS0ID_SBYTE: { UA_SByte  v = (UA_SByte)jVal["Value"].get<int64_t>();  setNum(v, &UA_TYPES[UA_TYPES_SBYTE]);  break; }
        case UA_NS0ID_BYTE: { UA_Byte   v = (UA_Byte)jVal["Value"].get<uint64_t>(); setNum(v, &UA_TYPES[UA_TYPES_BYTE]);   break; }
        case UA_NS0ID_INT16: { UA_Int16  v = (UA_Int16)jVal["Value"].get<int64_t>();  setNum(v, &UA_TYPES[UA_TYPES_INT16]);  break; }
        case UA_NS0ID_UINT16: { UA_UInt16 v = (UA_UInt16)jVal["Value"].get<uint64_t>(); setNum(v, &UA_TYPES[UA_TYPES_UINT16]); break; }
        case UA_NS0ID_INT32: { UA_Int32  v = (UA_Int32)jVal["Value"].get<int64_t>();  setNum(v, &UA_TYPES[UA_TYPES_INT32]);  break; }
        case UA_NS0ID_UINT32: { UA_UInt32 v = (UA_UInt32)jVal["Value"].get<uint64_t>(); setNum(v, &UA_TYPES[UA_TYPES_UINT32]); break; }
        case UA_NS0ID_INT64: { UA_Int64  v = (UA_Int64)jVal["Value"].get<int64_t>();  setNum(v, &UA_TYPES[UA_TYPES_INT64]);  break; }
        case UA_NS0ID_UINT64: { UA_UInt64 v = (UA_UInt64)jVal["Value"].get<uint64_t>(); setNum(v, &UA_TYPES[UA_TYPES_UINT64]); break; }
        case UA_NS0ID_FLOAT: { UA_Float  v = (UA_Float)jVal["Value"].get<double>();   setNum(v, &UA_TYPES[UA_TYPES_FLOAT]);  break; }
        case UA_NS0ID_DOUBLE: { UA_Double v = (UA_Double)jVal["Value"].get<double>();   setNum(v, &UA_TYPES[UA_TYPES_DOUBLE]); break; }
        case UA_NS0ID_DATETIME: {
            UA_Double unixSec = jVal["Value"].get<double>();
            UA_DateTime dt = UA_DateTime_fromUnixTime((UA_Int64)unixSec);
            setNum(dt, &UA_TYPES[UA_TYPES_DATETIME]);
            break;
        }
        case UA_NS0ID_STRING:
        default: {
            std::string s = jVal["Value"].is_string() ? jVal["Value"].get<std::string>()
                : jVal["Value"].dump();
            UA_String ua = UA_STRING_ALLOC(s.c_str());
            UA_Variant_setScalarCopy(&out->value, &ua, &UA_TYPES[UA_TYPES_STRING]);
            UA_String_clear(&ua);
            break;
        }
        }
    }
    catch (...) {
        // Fallback: convert any type to string on error
        std::string s = jVal["Value"].dump();
        UA_String ua = UA_STRING_ALLOC(s.c_str());
        UA_Variant_setScalarCopy(&out->value, &ua, &UA_TYPES[UA_TYPES_STRING]);
        UA_String_clear(&ua);
    }

    // Stop t2 latency measurement: value leaves server to client
    g_latencyTracker.stopT2(key);
}

/**
 * @brief Loads all trusted client certificates from a directory
 *
 * Scans the specified directory for .der certificate files and loads them
 * into a list for server configuration. These certificates are trusted for
 * client authentication when using certificate-based security.
 *
 * @param trustDir Path to the directory containing trusted certificates
 * @return Vector of UA_ByteString objects containing certificate data
 */
static std::vector<UA_ByteString> loadTrustList(const std::string& trustDir) {
    std::vector<UA_ByteString> trustList;

    namespace fs = std::filesystem;
    if (!fs::exists(trustDir)) {
        std::cout << "[Security] Trust directory not found: " << trustDir << std::endl;
        return trustList;
    }

    for (const auto& entry : fs::directory_iterator(trustDir)) {
        if (entry.path().extension() == ".der") {
            std::string path = entry.path().string();
            UA_ByteString cert = loadCertificate(path.c_str());
            if (cert.length > 0) {
                trustList.push_back(cert);
                std::cout << "[Security] Added to TrustList: " << entry.path().filename() << std::endl;
            }
        }
    }

    return trustList;
}

void runServer(UA_UInt16 port, const std::string& plcAlias, const std::string& mode, bool latencyTestEnabled) {
    UA_Server* server = UA_Server_new();
    if (!server) return;

    // Enable latency testing if configured
    if (latencyTestEnabled) {
        g_latencyTracker.setEnabled(true);
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "Starting Secure OPC UA Server" << std::endl;
    std::cout << "Port: " << port << " | Alias: " << plcAlias << " | Mode: " << mode << std::endl;
    if (latencyTestEnabled) {
        std::cout << "Latency Test: ENABLED" << std::endl;
    }
    std::cout << "========================================\n" << std::endl;

    // Load certificates for Basic256Sha256 security
    UA_ByteString certificate = loadCertificate("server_cert.der");
    UA_ByteString privateKey = loadPrivateKey("server_key.der");
    auto trustList = loadTrustList("certs/trusted");

    bool securityEnabled = false;

    if (certificate.length > 0 && privateKey.length > 0) {
        std::cout << "[Security] Configuring server with Basic256Sha256..." << std::endl;

#ifdef UA_ENABLE_ENCRYPTION
        // Encryption is available - use full security
        UA_StatusCode retval = UA_ServerConfig_setDefaultWithSecurityPolicies(
            UA_Server_getConfig(server),
            port,
            &certificate,
            &privateKey,
            trustList.empty() ? nullptr : trustList.data(),
            trustList.empty() ? 0 : trustList.size(),
            nullptr, 0,
            nullptr, 0
        );

        if (retval == UA_STATUSCODE_GOOD) {
            std::cout << "[Security] Basic256Sha256 enabled successfully!" << std::endl;
            std::cout << "[Security] Certificate-based authentication active" << std::endl;
            std::cout << "[Security] Available endpoints:" << std::endl;
            std::cout << "[Security]   - opc.tcp://0.0.0.0:" << port << " (None)" << std::endl;
            std::cout << "[Security]   - opc.tcp://0.0.0.0:" << port << " (SignAndEncrypt)" << std::endl;
            securityEnabled = true;
        }
        else {
            std::cerr << "[Security] Failed to configure security: "
                << UA_StatusCode_name(retval) << std::endl;
            std::cerr << "[Security] Falling back to minimal config (no security)" << std::endl;
            UA_ServerConfig_setMinimal(UA_Server_getConfig(server), port, nullptr);
        }
#else
        // Encryption NOT available in this build of open62541
        std::cerr << "[Security] open62541 was built WITHOUT encryption support!" << std::endl;
        std::cerr << "[Security] Cannot use Basic256Sha256 security policy" << std::endl;
        std::cerr << "[Security] To enable encryption, rebuild open62541 with:" << std::endl;
        std::cerr << "[Security]  cmake -DUA_ENABLE_ENCRYPTION=ON" << std::endl;
        std::cerr << "[Security] Falling back to minimal config (INSECURE!)" << std::endl;
        UA_ServerConfig_setMinimal(UA_Server_getConfig(server), port, nullptr);
#endif

        UA_ByteString_clear(&certificate);
        UA_ByteString_clear(&privateKey);
        for (auto& cert : trustList) {
            UA_ByteString_clear(&cert);
        }

    }
    else {
        std::cerr << "[Security] Certificates not found or invalid!" << std::endl;
        std::cerr << "[Security] Server will run WITHOUT encryption (insecure mode)" << std::endl;
        std::cerr << "[Security] To enable security:" << std::endl;
        std::cerr << "[Security]   1. Generate certificates using OpenSSL" << std::endl;
        std::cerr << "[Security]   2. Place server_cert.der and server_key.der in certs/server/ folder" << std::endl;
        std::cerr << "[Security] Falling back to minimal config..." << std::endl;

        UA_ServerConfig_setMinimal(UA_Server_getConfig(server), port, nullptr);
    }

    // Step 1: Retrieve ApplicationURI from PLC namespace (index 1)
    std::string nsMapKey = plcAlias + std::string(":1");
    std::string appUri = "urn:SigmaServer:" + plcAlias;  // Fallback
    {
        nlohmann::json uriJson;
        if (g_plcNamespaceMap.get(nsMapKey, uriJson)) {
            appUri = uriJson.get<std::string>();  // Use original PLC URI
        }
    }

    if (securityEnabled) {
        std::cout << "[Security] Server will use ApplicationURI: " << appUri << std::endl;
        std::cout << "[Security] This URI must be present in the certificate's SAN!" << std::endl;

        if (appUri.find("default.plc") != std::string::npos) {
            std::cerr << "[Security] WARNING: Using fallback URI - PLC namespace not loaded yet?" << std::endl;
        }
    }

    overrideDefaultApplicationUri(server, appUri);

    // Step 2: Map all PLC namespaces to server namespaces
    auto nsArrayJson = g_plcNamespaceMap.getAll();
    std::unordered_map<std::string, UA_UInt16> serverNsMap;

    // Collect all namespace entries for this PLC
    std::vector<std::pair<std::string, std::string>> nsEntries; // (key, uri)
    for (auto it = nsArrayJson.begin(); it != nsArrayJson.end(); ++it) {
        const std::string& key = it.key();
        if (key.rfind(plcAlias + ":", 0) != 0) continue;
        const std::string uri = it.value().get<std::string>();
        nsEntries.push_back({ key, uri });
    }

    // Sort by namespace index to ensure correct order
    std::sort(nsEntries.begin(), nsEntries.end(), [&plcAlias](const auto& a, const auto& b) {
        size_t idxA = std::stoi(a.first.substr(plcAlias.length() + 1));
        size_t idxB = std::stoi(b.first.substr(plcAlias.length() + 1));
        return idxA < idxB;
        });

    // Map PLC namespaces to server namespaces
    for (const auto& entry : nsEntries) {
        // Extract namespace index from key (e.g., "PLC21:1" -> 1)
        size_t colonPos = entry.first.find(':');
        int plcNsIndex = std::stoi(entry.first.substr(colonPos + 1));

        UA_String uaUri = UA_STRING_ALLOC(entry.second.c_str());
        size_t existingIdx = 0;
        UA_StatusCode checkResult = UA_Server_getNamespaceByName(server, uaUri, &existingIdx);

        UA_UInt16 sNs;
        if (plcNsIndex == 1 && checkResult != UA_STATUSCODE_GOOD) {
            // Force map it to server NS1 to preserve index alignment
            sNs = 1;
        }
        else if (checkResult == UA_STATUSCODE_GOOD) {
            // Namespace already exists in server - use existing index
            sNs = (UA_UInt16)existingIdx;
        }
        else {
            // Namespace doesn't exist - add it
            sNs = UA_Server_addNamespace(server, entry.second.c_str());
        }

        serverNsMap[entry.first] = sNs;
        UA_String_clear(&uaUri);
    }

    auto allStruct = g_structureMap.getAll();
    std::vector<std::string> keys;
    keys.reserve(allStruct.size());
    for (auto it = allStruct.begin(); it != allStruct.end(); ++it) {
        if (it.key().rfind(plcAlias + ":", 0) == 0)
            keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());

    std::unordered_map<std::string, UA_NodeId> folderMap;
    std::vector<UA_NodeId> publishedVariables;

    for (const auto& key : keys) {
        const auto& structure = allStruct[key];

        const size_t c1 = key.find(':'), c2 = key.find(':', c1 + 1);
        UA_UInt16 clientNs = (UA_UInt16)std::stoi(key.substr(c1 + 1, c2 - c1 - 1));
        UA_UInt32 nodeNum = (UA_UInt32)std::stoul(key.substr(c2 + 1));

        {
            nlohmann::json uriJson;
            const std::string mapKey = plcAlias + ":" + std::to_string(clientNs);
            if (g_plcNamespaceMap.get(mapKey, uriJson)) {
                const std::string uri = uriJson.get<std::string>();
                if (uri == "http://opcfoundation.org/UA/")
                    continue;
            }
        }

        const std::string mapKey2 = plcAlias + ":" + std::to_string(clientNs);
        if (!serverNsMap.count(mapKey2)) continue;
        UA_UInt16 serverNs = serverNsMap[mapKey2];

        UA_NodeId parentId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
        std::string accum = "Objects";
        if (structure.contains("Path") && structure["Path"].is_array()) {
            for (size_t i = 1; i < structure["Path"].size(); ++i) {
                const std::string seg = structure["Path"][i].get<std::string>();
                accum += "/" + seg;
                if (!folderMap.count(accum)) {
                    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
                    oAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", seg.c_str());
                    UA_NodeId fId;
                    UA_StatusCode st = UA_Server_addObjectNode(
                        server,
                        UA_NODEID_NULL,
                        parentId,
                        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                        UA_QUALIFIEDNAME_ALLOC(serverNs, seg.c_str()),
                        UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                        oAttr,
                        nullptr,
                        &fId);
                    if (st == UA_STATUSCODE_GOOD) folderMap[accum] = fId;
                }
                if (folderMap.count(accum))
                    parentId = folderMap[accum];
            }
        }

        UA_NodeId varId = UA_NODEID_NUMERIC(serverNs, nodeNum);
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        const std::string name = structure.value("BrowseName",
            structure.value("DisplayName", "Node"));
        attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", name.c_str());

        UA_UInt32 dtId = UA_NS0ID_STRING;
        try {
            if (structure.contains("DataType"))
                dtId = (UA_UInt32)std::stoul(structure["DataType"].get<std::string>());
        }
        catch (...) { dtId = UA_NS0ID_STRING; }
        attr.dataType = UA_NODEID_NUMERIC(0, dtId);

        switch (dtId) {
        case UA_NS0ID_BOOLEAN: { UA_Boolean v = false; UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_BOOLEAN]); break; }
        case UA_NS0ID_SBYTE: { UA_SByte   v = 0;     UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_SBYTE]);   break; }
        case UA_NS0ID_BYTE: { UA_Byte    v = 0;     UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_BYTE]);    break; }
        case UA_NS0ID_INT16: { UA_Int16   v = 0;     UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_INT16]);   break; }
        case UA_NS0ID_UINT16: { UA_UInt16  v = 0;     UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_UINT16]);  break; }
        case UA_NS0ID_INT32: { UA_Int32   v = 0;     UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_INT32]);   break; }
        case UA_NS0ID_UINT32: { UA_UInt32  v = 0;     UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_UINT32]);  break; }
        case UA_NS0ID_INT64: { UA_Int64   v = 0;     UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_INT64]);   break; }
        case UA_NS0ID_UINT64: { UA_UInt64  v = 0;     UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_UINT64]);  break; }
        case UA_NS0ID_FLOAT: { UA_Float   v = 0.0f;  UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_FLOAT]);   break; }
        case UA_NS0ID_DOUBLE: { UA_Double  v = 0.0;   UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_DOUBLE]);  break; }
        case UA_NS0ID_DATETIME: { UA_DateTime v = UA_DateTime_now(); UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_DATETIME]); break; }
        case UA_NS0ID_STRING:
        default: { UA_String v = UA_STRING_ALLOC("No Value"); UA_Variant_setScalarCopy(&attr.value, &v, &UA_TYPES[UA_TYPES_STRING]); UA_String_clear(&v); }
        }

        UA_NodeId newId;
        UA_StatusCode stv = UA_Server_addVariableNode(
            server,
            varId,
            parentId,
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME_ALLOC(serverNs, name.c_str()),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
            attr,
            nullptr,
            &newId);
        if (stv != UA_STATUSCODE_GOOD) continue;

        char* ctx = new char[key.size() + 1];
        std::memcpy(ctx, key.c_str(), key.size());
        ctx[key.size()] = '\0';
        UA_ValueCallback cb; cb.onRead = dynamicValueReadCallback; cb.onWrite = nullptr;
        UA_Server_setVariableNode_valueCallback(server, newId, cb);
        UA_Server_setNodeContext(server, newId, ctx);

        publishedVariables.push_back(newId);
    }

    if (mode == "PubSub" || mode == "pubsub" || mode == "PUBSUB") {
        std::cout << "\n[PubSub] Configuring OPC UA PubSub mode..." << std::endl;

#ifdef UA_ENABLE_PUBSUB
        // Step 1: Create PubSub Connection (UDP multicast)
        UA_PubSubConnectionConfig connectionConfig;
        memset(&connectionConfig, 0, sizeof(connectionConfig));
        connectionConfig.name = UA_STRING_ALLOC("UDP Multicast Connection");
        connectionConfig.transportProfileUri = UA_STRING_ALLOC("http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp");
        connectionConfig.enabled = UA_TRUE;

        // Configure UDP multicast address
        UA_NetworkAddressUrlDataType networkAddressUrl;
        memset(&networkAddressUrl, 0, sizeof(UA_NetworkAddressUrlDataType));
        networkAddressUrl.networkInterface = UA_STRING_ALLOC("");
        networkAddressUrl.url = UA_STRING_ALLOC("opc.udp://224.0.0.22:4840/");

        UA_Variant_setScalar(&connectionConfig.address, &networkAddressUrl,
            &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);

        // Add PubSub connection
        UA_NodeId connectionId;
        UA_StatusCode retval = UA_Server_addPubSubConnection(server, &connectionConfig, &connectionId);

        // Cleanup connection config strings
        UA_String_clear(&connectionConfig.name);
        UA_String_clear(&connectionConfig.transportProfileUri);
        UA_String_clear(&networkAddressUrl.networkInterface);
        UA_String_clear(&networkAddressUrl.url);

        if (retval != UA_STATUSCODE_GOOD) {
            std::cerr << "[PubSub] Failed to add PubSub connection: "
                << UA_StatusCode_name(retval) << std::endl;
        }
        else {
            std::cout << "[PubSub] Created UDP multicast connection on 224.0.0.22:4840" << std::endl;

            // Step 2: Create PublishedDataSet
            UA_PublishedDataSetConfig pdsConfig;
            memset(&pdsConfig, 0, sizeof(UA_PublishedDataSetConfig));
            pdsConfig.publishedDataSetType = UA_PUBSUB_DATASET_PUBLISHEDITEMS;
            pdsConfig.name = UA_STRING_ALLOC((plcAlias + " DataSet").c_str());

            UA_NodeId publishedDataSetId;
            retval = UA_Server_addPublishedDataSet(server, &pdsConfig, &publishedDataSetId).addResult;
            if (retval != UA_STATUSCODE_GOOD) {
                std::cerr << "[PubSub] Failed to add PublishedDataSet: "
                    << UA_StatusCode_name(retval) << std::endl;
                UA_PublishedDataSetConfig_clear(&pdsConfig);
            }
            else {
                std::cout << "[PubSub] Created PublishedDataSet for " << plcAlias << std::endl;

                // Step 3: Add all published variables to the DataSet
                int fieldCount = 0;
                for (const auto& varId : publishedVariables) {
                    UA_DataSetFieldConfig fieldConfig;
                    memset(&fieldConfig, 0, sizeof(UA_DataSetFieldConfig));
                    fieldConfig.dataSetFieldType = UA_PUBSUB_DATASETFIELD_VARIABLE;
                    fieldConfig.field.variable.publishParameters.publishedVariable = varId;
                    fieldConfig.field.variable.publishParameters.attributeId = UA_ATTRIBUTEID_VALUE;

                    UA_NodeId fieldId;
                    UA_StatusCode fieldResult = UA_Server_addDataSetField(server, publishedDataSetId,
                        &fieldConfig, &fieldId).result;
                    if (fieldResult == UA_STATUSCODE_GOOD) {
                        fieldCount++;
                    }
                }
                std::cout << "[PubSub] Added " << fieldCount << " fields to DataSet" << std::endl;

                // Step 4: Create WriterGroup
                UA_WriterGroupConfig writerGroupConfig;
                memset(&writerGroupConfig, 0, sizeof(UA_WriterGroupConfig));
                writerGroupConfig.name = UA_STRING_ALLOC("WriterGroup");
                writerGroupConfig.publishingInterval = 100.0;  // 100ms publishing interval
                writerGroupConfig.enabled = UA_FALSE;
                writerGroupConfig.writerGroupId = 100;
                writerGroupConfig.encodingMimeType = UA_PUBSUB_ENCODING_UADP;
                writerGroupConfig.messageSettings.encoding = UA_EXTENSIONOBJECT_DECODED;
                writerGroupConfig.messageSettings.content.decoded.type = &UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE];

                UA_UadpWriterGroupMessageDataType writerGroupMessage;
                UA_UadpWriterGroupMessageDataType_init(&writerGroupMessage);
                writerGroupMessage.networkMessageContentMask =
                    (UA_UadpNetworkMessageContentMask)(UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID |
                        UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER |
                        UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID |
                        UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER);
                writerGroupConfig.messageSettings.content.decoded.data = &writerGroupMessage;

                UA_NodeId writerGroupId;
                retval = UA_Server_addWriterGroup(server, connectionId, &writerGroupConfig, &writerGroupId);

                UA_String_clear(&writerGroupConfig.name);

                if (retval != UA_STATUSCODE_GOOD) {
                    std::cerr << "[PubSub] Failed to add WriterGroup: "
                        << UA_StatusCode_name(retval) << std::endl;
                }
                else {
                    std::cout << "[PubSub] Created WriterGroup with 100ms publishing interval" << std::endl;

                    // Step 5: Create DataSetWriter
                    UA_DataSetWriterConfig dataSetWriterConfig;
                    memset(&dataSetWriterConfig, 0, sizeof(UA_DataSetWriterConfig));
                    dataSetWriterConfig.name = UA_STRING_ALLOC("DataSetWriter");
                    dataSetWriterConfig.dataSetWriterId = 62541;
                    dataSetWriterConfig.keyFrameCount = 10;

                    UA_NodeId dataSetWriterId;
                    retval = UA_Server_addDataSetWriter(server, writerGroupId, publishedDataSetId,
                        &dataSetWriterConfig, &dataSetWriterId);

                    UA_String_clear(&dataSetWriterConfig.name);

                    if (retval != UA_STATUSCODE_GOOD) {
                        std::cerr << "[PubSub] Failed to add DataSetWriter: "
                            << UA_StatusCode_name(retval) << std::endl;
                    }
                    else {
                        std::cout << "[PubSub] Created DataSetWriter" << std::endl;

                        // Step 6: Freeze and enable the WriterGroup to start publishing
                        retval = UA_Server_freezeWriterGroupConfiguration(server, writerGroupId);
                        if (retval != UA_STATUSCODE_GOOD) {
                            std::cerr << "[PubSub] ✗ Failed to freeze WriterGroup configuration: "
                                << UA_StatusCode_name(retval) << std::endl;
                        }
                        else {
                            retval = UA_Server_setWriterGroupOperational(server, writerGroupId);
                            if (retval != UA_STATUSCODE_GOOD) {
                                std::cerr << "[PubSub] Failed to set WriterGroup operational: "
                                    << UA_StatusCode_name(retval) << std::endl;
                            }
                            else {
                                std::cout << "[PubSub] WriterGroup enabled - publishing started!" << std::endl;
                                std::cout << "[PubSub] Multicast Address: 224.0.0.22:4840" << std::endl;
                                std::cout << "[PubSub] Publishing " << fieldCount << " variables every 100ms" << std::endl;
                            }
                        }
                    }
                }
                UA_PublishedDataSetConfig_clear(&pdsConfig);
            }
        }
#else
        std::cerr << "[PubSub] ERROR: PubSub mode requested but UA_ENABLE_PUBSUB is not enabled!" << std::endl;
        std::cerr << "[PubSub] Please rebuild with -DUA_ENABLE_PUBSUB=ON in CMakeLists.txt" << std::endl;
#endif
    }

    std::cout << "\n[Server] Starting server loop on port " << port << "..." << std::endl;
    if (securityEnabled) {
        std::cout << "[Server] Security: ENABLED (Basic256Sha256 + Certificate Auth)" << std::endl;
    }
    else {
        std::cout << "[Server] Security: DISABLED (Insecure mode)" << std::endl;
    }
    std::cout << "[Server] Press Ctrl+C to stop\n" << std::endl;
    UA_Server_run_startup(server);
    // Use blocking iterate to reduce CPU load
    while (StoppableThread::running) {
        UA_Server_run_iterate(server, true);  // true = internal wait, no busy-wait
    }
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    std::cout << "[Server] Server on port " << port << " stopped." << std::endl;
}