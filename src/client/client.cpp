/**
 * @file client.cpp
 * @brief Implementation of OPC UA and Modbus client functionality
 *
 * Implements clients for connecting to OPC UA PLCs and Modbus TCP devices:
 * - OPC UA client: Discovers node structures, reads values, handles method calls
 * - Modbus TCP client: Reads registers and converts to OPC UA-compatible format
 * - Data storage: Writes all data to global thread-safe maps (g_dataMap, g_structureMap)
 * - Latency tracking: Measures t1 latency (read to dataMap storage)
 * - Resilience: Automatic reconnection, fallback endpoints, error handling
 */

#include "client.h"
#include "SharedData.h"
#include "config.h"
#include "utils.h"
#include "StoppableThread.h"
#include "LatencyTracking.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <sstream>
#include <ctime>
#include <modbus.h>
#include <cerrno>
#include <stdlib.h>
#include <stdlib.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <algorithm>


namespace fs = std::filesystem;

/**
 * @brief Recursive helper function to build the browse path from root to a given node
 *
 * Traverses the OPC UA address space backwards (using inverse references) to construct
 * the full path from the root folder to the target node. This path is used for generating
 * human-readable node identifiers.
 *
 * @param client Connected OPC UA client instance
 * @param nodeId Node identifier to trace back to root
 * @return Vector of strings representing the path components (e.g., ["Objects", "DeviceSet", "PLC1"])
 */
static std::vector<std::string> buildPath(UA_Client* client, const UA_NodeId& nodeId) {
    // Base case: reached a standard root folder
    if (nodeId.identifier.numeric == UA_NS0ID_OBJECTSFOLDER) return { "Objects" };
    if (nodeId.identifier.numeric == UA_NS0ID_TYPESFOLDER)   return { "Types" };
    if (nodeId.identifier.numeric == UA_NS0ID_VIEWSFOLDER)   return { "Views" };

    // Inverse browse to find parent nodes via 'Organizes' references
    UA_BrowseRequest req;
    UA_BrowseRequest_init(&req);
    req.nodesToBrowse = (UA_BrowseDescription*)UA_Array_new(1, &UA_TYPES[UA_TYPES_BROWSEDESCRIPTION]);
    req.nodesToBrowseSize = 1;
    UA_BrowseDescription_init(&req.nodesToBrowse[0]);
    req.nodesToBrowse[0].nodeId = nodeId;
    req.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_INVERSE;
    req.nodesToBrowse[0].referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    req.nodesToBrowse[0].includeSubtypes = true;
    req.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_BROWSENAME;

    UA_BrowseResponse resp = UA_Client_Service_browse(client, req);
    std::vector<std::string> path;
    if (resp.resultsSize > 0 && resp.results[0].referencesSize > 0) {
        UA_ReferenceDescription ref = resp.results[0].references[0];
        std::string name((char*)ref.browseName.name.data, ref.browseName.name.length);

        UA_NodeId parentId = UA_NODEID_NULL;
        if (ref.nodeId.nodeId.identifierType == UA_NODEIDTYPE_NUMERIC)
            parentId = UA_NODEID_NUMERIC(ref.nodeId.nodeId.namespaceIndex,
                ref.nodeId.nodeId.identifier.numeric);
        else if (ref.nodeId.nodeId.identifierType == UA_NODEIDTYPE_STRING)
            parentId = UA_NODEID_STRING(ref.nodeId.nodeId.namespaceIndex,
                (char*)ref.nodeId.nodeId.identifier.string.data);

        UA_BrowseResponse_clear(&resp);
        UA_BrowseRequest_clear(&req);

        // Recursive call
        auto parentPath = buildPath(client, parentId);
        parentPath.push_back(name);
        return parentPath;
    }
    UA_BrowseResponse_clear(&resp);
    UA_BrowseRequest_clear(&req);

    return { "Objects" };
}

/**
 * @brief Extracts a PLC folder name from an OPC UA endpoint URL
 *
 * Generates a folder name from the endpoint by extracting the last octet of the IP address.
 * Example: "opc.tcp://172.17.22.21:4840" -> "PLC21"
 *
 * @param endpoint The OPC UA endpoint URL
 * @return PLC folder name (e.g., "PLC21") or "PLC_unknown" if parsing fails
 */
std::string getPLCFolderName(const std::string& endpoint) {
    std::string prefix = "opc.tcp://";
    size_t pos = endpoint.find(prefix);
    if (pos == std::string::npos) return "PLC_unknown";
    std::string withoutPrefix = endpoint.substr(pos + prefix.size());
    size_t colonPos = withoutPrefix.find(':');
    std::string ip = withoutPrefix.substr(0, colonPos);
    size_t lastDot = ip.rfind('.');
    if (lastDot == std::string::npos) return "PLC_" + ip;
    std::string lastOctet = ip.substr(lastDot + 1);
    return "PLC" + lastOctet;
}

/**
 * @brief Reads the namespace array from a PLC and stores namespace URIs in g_plcNamespaceMap
 *
 * Reads the standard NamespaceArray node (i=2255) from the PLC and creates entries
 * in the global namespace map for each namespace URI. This mapping is required for
 * proper namespace handling when creating server nodes.
 *
 * @param client Connected OPC UA client instance
 * @param plcFolder PLC alias/folder name for key generation
 */
void readAndStoreNamespaceArray(UA_Client* client, const std::string& plcFolder) {
    UA_NodeId nsArrayId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_NAMESPACEARRAY); // i=2255
    UA_Variant nsVariant;
    UA_Variant_init(&nsVariant);
    UA_StatusCode retval = UA_Client_readValueAttribute(client, nsArrayId, &nsVariant);
    if (retval == UA_STATUSCODE_GOOD && nsVariant.arrayLength > 0 && nsVariant.type == &UA_TYPES[UA_TYPES_STRING]) {
        size_t nsCount = nsVariant.arrayLength;
        UA_String* nsArray = (UA_String*)nsVariant.data;
        for (size_t i = 0; i < nsCount; i++) {
            std::string nsUri((char*)nsArray[i].data, nsArray[i].length);
            std::string mapKey = plcFolder + ":" + std::to_string(i);
            g_plcNamespaceMap.set(mapKey, nsUri);
            std::cout << "PLC " << plcFolder << " Namespace " << i << " URI: " << nsUri << std::endl;
        }
    }
    else {
        std::cerr << "Error reading NamespaceArray for PLC " << plcFolder << std::endl;
    }
    UA_Variant_clear(&nsVariant);
}

void loadNodeStructure(UA_Client* client, const NodeConfig& nodeConfig, const std::string& alias) {
    UA_NodeId nodeId = UA_NODEID_NUMERIC(nodeConfig.namespaceIndex, nodeConfig.nodeId);
    const size_t ATTR_COUNT = 4;

    // Read node attributes (DisplayName, Description, DataType, BrowseName)
    UA_ReadRequest rReq;
    UA_ReadRequest_init(&rReq);
    rReq.nodesToRead = (UA_ReadValueId*)UA_Array_new(ATTR_COUNT, &UA_TYPES[UA_TYPES_READVALUEID]);
    rReq.nodesToReadSize = ATTR_COUNT;

    auto setReadValueId = [&](size_t idx, UA_AttributeId attrId) {
        UA_ReadValueId_init(&rReq.nodesToRead[idx]);
        rReq.nodesToRead[idx].attributeId = attrId;
        rReq.nodesToRead[idx].nodeId = nodeId;
        };
    setReadValueId(0, UA_ATTRIBUTEID_DISPLAYNAME);
    setReadValueId(1, UA_ATTRIBUTEID_DESCRIPTION);
    setReadValueId(2, UA_ATTRIBUTEID_DATATYPE);
    setReadValueId(3, UA_ATTRIBUTEID_BROWSENAME);

    UA_ReadResponse rResp = UA_Client_Service_read(client, rReq);

    nlohmann::json j;
    j["NamespaceIndex"] = nodeConfig.namespaceIndex;
    j["NodeID"] = nodeConfig.nodeId;

    for (size_t i = 0; i < rResp.resultsSize; i++) {
        UA_DataValue dv = rResp.results[i];
        if (dv.status != UA_STATUSCODE_GOOD) {
            if (rReq.nodesToRead[i].attributeId == UA_ATTRIBUTEID_DESCRIPTION &&
                dv.status == UA_STATUSCODE_BADATTRIBUTEIDINVALID) {
                continue;
            }
            if (rReq.nodesToRead[i].attributeId == UA_ATTRIBUTEID_DATATYPE && dv.value.data == NULL) {
                j["DataType"] = "NULL";
                continue;
            }
            continue;
        }
        switch (rReq.nodesToRead[i].attributeId) {
        case UA_ATTRIBUTEID_DISPLAYNAME: {
            if (dv.value.type == &UA_TYPES[UA_TYPES_LOCALIZEDTEXT] && dv.value.data) {
                UA_LocalizedText* lt = (UA_LocalizedText*)dv.value.data;
                j["DisplayName"] = std::string((char*)lt->text.data, lt->text.length);
            }
        } break;
        case UA_ATTRIBUTEID_DESCRIPTION: {
            if (dv.value.type == &UA_TYPES[UA_TYPES_LOCALIZEDTEXT] && dv.value.data) {
                UA_LocalizedText* lt = (UA_LocalizedText*)dv.value.data;
                j["Description"] = std::string((char*)lt->text.data, lt->text.length);
            }
        } break;
        case UA_ATTRIBUTEID_DATATYPE: {
            if (dv.value.data == NULL) {
                j["DataType"] = "NULL";
            }
            else if (dv.value.type == &UA_TYPES[UA_TYPES_NODEID]) {
                UA_NodeId* dtNodeId = (UA_NodeId*)dv.value.data;
                j["DataType"] = std::to_string(dtNodeId->identifier.numeric);
            }
        } break;
        case UA_ATTRIBUTEID_BROWSENAME: {
            if (dv.value.type == &UA_TYPES[UA_TYPES_QUALIFIEDNAME] && dv.value.data) {
                UA_QualifiedName* qn = (UA_QualifiedName*)dv.value.data;
                j["BrowseName"] = std::string((char*)qn->name.data, qn->name.length);
            }
        } break;
        }
    }
    UA_ReadResponse_clear(&rResp);
    UA_Array_delete(rReq.nodesToRead, rReq.nodesToReadSize, &UA_TYPES[UA_TYPES_READVALUEID]);

    // Build browse path and insert alias as 2nd segment: ["Objects", "<alias>", ...]
    auto pathVec = buildPath(client, nodeId);
    if (pathVec.empty() || pathVec[0] != "Objects")
        pathVec.insert(pathVec.begin(), "Objects");
    if (pathVec.size() == 1 || pathVec[1] != alias)
        pathVec.insert(pathVec.begin() + 1, alias);
    j["Path"] = pathVec;

    // Store structure in global map
    std::string key = getDataKey(alias, nodeConfig.namespaceIndex, nodeConfig.nodeId);
    g_structureMap.set(key, j);

    // Update namespace JSON file with minimal node entry (NamespaceIndex/NodeID)
    fs::path nsFile = getNamespaceJsonPath(alias, nodeConfig.namespaceIndex);
    nlohmann::json nsJson = nlohmann::json::array();
    if (fs::exists(nsFile)) {
        std::ifstream inFile(nsFile);
        if (inFile.is_open())
            inFile >> nsJson;
    }
    bool exists = false;
    for (auto& e : nsJson) {
        if (e.contains("NodeID") &&
            e["NodeID"].get<UA_UInt32>() == nodeConfig.nodeId) {
            exists = true; break;
        }
    }
    if (!exists) {
        nlohmann::json minimal;
        minimal["NamespaceIndex"] = nodeConfig.namespaceIndex;
        minimal["NodeID"] = nodeConfig.nodeId;
        nsJson.push_back(minimal);
        std::ofstream out(nsFile);
        if (out.is_open()) {
            out << nsJson.dump(4);
        }
    }
}

/**
 * @brief Checks if the OPC UA client is currently connected to the server
 *
 * @param client The OPC UA client to check
 * @return true if connected, false otherwise
 */
static bool isClientConnected(UA_Client* client) {
    if (!client) return false;
    UA_SecureChannelState channelState;
    UA_SessionState sessionState;
    UA_StatusCode connectStatus;
    UA_Client_getState(client, &channelState, &sessionState, &connectStatus);
    return (sessionState == UA_SESSIONSTATE_ACTIVATED &&
            channelState == UA_SECURECHANNELSTATE_OPEN);
}

/**
 * @brief Attempts to reconnect the client with exponential backoff
 *
 * @param client The client to reconnect
 * @param endpoint Primary endpoint URL
 * @param fallbackEndpoint Fallback endpoint URL (optional)
 * @param maxRetries Maximum number of retry attempts
 * @return true if reconnection successful, false otherwise
 */
static bool reconnectClient(UA_Client* client, const std::string& endpoint,
                           const std::string& fallbackEndpoint = "", int maxRetries = 3) {
    // Check if already connected to avoid "Client connection already initiated" error
    if (isClientConnected(client)) {
        return true;
    }

    // First disconnect if needed
    UA_Client_disconnect(client);

    for (int retry = 0; retry < maxRetries; ++retry) {
        if (retry > 0) {
            // Exponential backoff: 1s, 2s, 4s
            int delaySec = 1 << retry;
            std::cout << "info/client        Retry " << retry << "/" << maxRetries
                     << " after " << delaySec << "s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(delaySec));
        }

        UA_StatusCode retval = UA_Client_connect(client, endpoint.c_str());
        if (retval == UA_STATUSCODE_GOOD) {
            std::cout << "info/client        Successfully reconnected to " << endpoint << std::endl;
            return true;
        }

        // Provide detailed error information for security-related failures
        if (retval == UA_STATUSCODE_BADSECURITYCHECKSFAILED) {
            std::cerr << "error/client       Security checks failed during reconnection!" << std::endl;
            std::cerr << "                   Possible causes:" << std::endl;
            std::cerr << "                   - Certificate validation failed" << std::endl;
            std::cerr << "                   - Clock synchronization issue (check system time)" << std::endl;
            std::cerr << "                   - Security policy mismatch" << std::endl;
            std::cerr << "                   - Nonce validation failed" << std::endl;
        } else if (retval == UA_STATUSCODE_BADSECURECHANNELCLOSED) {
            std::cerr << "warn/client        Secure channel was closed by server" << std::endl;
        } else if (retval == UA_STATUSCODE_BADSESSIONCLOSED) {
            std::cerr << "warn/client        Session was closed by server" << std::endl;
        } else {
            std::cerr << "warn/client        Reconnect attempt " << (retry + 1)
                     << " failed: " << UA_StatusCode_name(retval) << std::endl;
        }

        // Try fallback endpoint if available
        if (!fallbackEndpoint.empty() && retry == 0) {
            retval = UA_Client_connect(client, fallbackEndpoint.c_str());
            if (retval == UA_STATUSCODE_GOOD) {
                std::cout << "info/client        Successfully reconnected to fallback "
                         << fallbackEndpoint << std::endl;
                return true;
            }
        }
    }

    std::cerr << "error/client       Failed to reconnect after " << maxRetries << " attempts" << std::endl;
    return false;
}

/**
 * Updates the current value of a node - handles all common OPC UA data types.
 * This function is called periodically to read the latest values from the PLC.
 * Includes automatic reconnection logic on connection failures.
 */
void updateNodeValue(UA_Client* client, const NodeConfig& nodeConfig, const std::string& alias) {
    UA_NodeId nodeId = UA_NODEID_NUMERIC(nodeConfig.namespaceIndex, nodeConfig.nodeId);
    UA_Variant value;
    UA_Variant_init(&value);

    // Network call to PLC (not part of aggregation server latency measurement)
    UA_StatusCode retval = UA_Client_readValueAttribute(client, nodeId, &value);
    if (retval != UA_STATUSCODE_GOOD) {
        // Check if it's a connection-related error
        if (retval == UA_STATUSCODE_BADSECURECHANNELCLOSED ||
            retval == UA_STATUSCODE_BADSESSIONCLOSED ||
            retval == UA_STATUSCODE_BADCONNECTIONCLOSED ||
            retval == UA_STATUSCODE_BADSECURITYCHECKSFAILED) {
            std::cerr << "warn/client        Connection lost (node " << nodeConfig.nodeId
                     << "): " << UA_StatusCode_name(retval) << std::endl;
            // Don't attempt reconnect here - let the main loop handle it
            return;
        }

        std::cerr << "Error reading value for node " << nodeConfig.nodeId << ": "
            << UA_StatusCode_name(retval) << std::endl;
        return;
    }

    // t1 Start: Data received from PLC, aggregation server processing begins
    std::string key = getDataKey(alias, nodeConfig.namespaceIndex, nodeConfig.nodeId);
    g_latencyTracker.startT1(key);

    nlohmann::json j;
    j["NamespaceIndex"] = nodeConfig.namespaceIndex;
    j["NodeID"] = nodeConfig.nodeId;

    if (value.data == NULL) {
        j["Value"] = nullptr;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        j["Value"] = *(UA_Boolean*)value.data;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT16])) {
        j["Value"] = *(UA_Int16*)value.data;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT32])) {
        j["Value"] = *(UA_Int32*)value.data;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT64])) {
        j["Value"] = *(UA_Int64*)value.data;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT16])) {
        j["Value"] = *(UA_UInt16*)value.data;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
        j["Value"] = *(UA_UInt32*)value.data;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT64])) {
        j["Value"] = *(UA_UInt64*)value.data;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT])) {
        j["Value"] = *(float*)value.data;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_DOUBLE])) {
        j["Value"] = *(double*)value.data;
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_STRING])) {
        UA_String uaStr = *(UA_String*)value.data;
        j["Value"] = std::string((char*)uaStr.data, uaStr.length);
    }
    else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_DATETIME])) {
        UA_DateTime dt = *(UA_DateTime*)value.data;
        j["Value"] = (double)UA_DateTime_toUnixTime(dt);
    }
    else {
        j["Value"] = "Unsupported type";
    }

    UA_Variant_clear(&value);

    // t1 Stop: Value is stored in global data store
    g_dataMap.set(key, j);
    g_latencyTracker.stopT1(key);
}

void callRemoteMethod(UA_Client* client, const PLCConfig& plcConfig) {
    if (!plcConfig.methodCallEnabled)
        return;

    UA_NodeId objectId = UA_NODEID_NUMERIC(plcConfig.methodNs, plcConfig.methodObjectId);
    UA_NodeId methodId = UA_NODEID_NUMERIC(plcConfig.methodNs, plcConfig.methodId);

    std::cout << "[InsecClient] Calling method: ns=" << plcConfig.methodNs
              << " objId=" << plcConfig.methodObjectId
              << " methId=" << plcConfig.methodId << std::endl;

    // 1 Dummy-Input-Argument (UInt32 = 0)
    UA_UInt32 dummy = 0;
    UA_Variant input;
    UA_Variant_init(&input);
    UA_Variant_setScalar(&input, &dummy, &UA_TYPES[UA_TYPES_UINT32]);
    size_t inputSize = 1;
    UA_Variant* inputArgs = &input;

    // Output arguments
    size_t outputSize = 0;
    UA_Variant* outputArgs = nullptr;

    UA_StatusCode rc = UA_Client_call(client, objectId, methodId,
                                      inputSize, inputArgs,
                                      &outputSize, &outputArgs);

    if (rc == UA_STATUSCODE_GOOD) {
        std::cout << "[InsecClient] Method call OK (ns=" << plcConfig.methodNs
                  << " obj=" << plcConfig.methodObjectId
                  << " meth=" << plcConfig.methodId
                  << "), outputs=" << outputSize << "\n";

        if (outputSize > 0 &&
            UA_Variant_hasScalarType(&outputArgs[0], &UA_TYPES[UA_TYPES_UINT32])) {
            UA_UInt32 v = *(UA_UInt32*)outputArgs[0].data;
            std::cout << "[InsecClient] Method return UInt32=" << v << "\n";
        }
    } else {
        std::cerr << "[InsecClient] Method call FAILED: 0x"
                  << std::hex << rc << std::dec
                  << " (" << UA_StatusCode_name(rc) << ")\n";
    }

    if (outputArgs)
        UA_Array_delete(outputArgs, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
}

void runPLCClientForEndpoint(const PLCConfig& plcConfig) {
    auto getEnv = [](const char* name) -> std::string {
#ifdef _WIN32
        char* buf = nullptr; size_t sz = 0;
        if (_dupenv_s(&buf, &sz, name) == 0 && buf) {
            std::string v(buf);
            free(buf);
            return v;
        }
        return {};
#else
        const char* v = std::getenv(name);
        return v ? std::string(v) : std::string();
#endif
        };

    bool preferFallback = false;
    std::string mode = getEnv("AGG_ENV");
    if (!mode.empty()) {
        if (mode == "LOCAL" || mode == "local")   preferFallback = true;
        if (mode == "DOCKER" || mode == "docker") preferFallback = false;
    }

    std::string first = plcConfig.endpoint;
    std::string second = plcConfig.endpointFallback;
    if (preferFallback && !plcConfig.endpointFallback.empty()) {
        first = plcConfig.endpointFallback;
        second = plcConfig.endpoint;
    }

    UA_Client* client = UA_Client_new();
    UA_ClientConfig* config = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(config);

    // Configure connection timeouts for more robust behavior
    config->timeout = 5000;  // 5 second timeout for service calls
    config->connectivityCheckInterval = 30000;  // Check connection every 30 seconds
    config->secureChannelLifeTime = 600000;  // 10 minutes secure channel lifetime

    UA_StatusCode retval = UA_Client_connect(client, first.c_str());
    std::string usedEndpoint = first;

    if (retval != UA_STATUSCODE_GOOD && !second.empty()) {
        std::cerr << "Connect failed for " << first << ": "
            << UA_StatusCode_name(retval) << " -> trying fallback " << second << "\n";
        retval = UA_Client_connect(client, second.c_str());
        if (retval == UA_STATUSCODE_GOOD)
            usedEndpoint = second;
    }

    if (retval != UA_STATUSCODE_GOOD) {
        std::cerr << "Error connecting to PLC at " << first << ": "
            << UA_StatusCode_name(retval) << std::endl;
        UA_Client_delete(client);
        return;
    }
    std::cout << "Connected to PLC at " << usedEndpoint << std::endl;

    std::string alias = plcConfig.alias.empty()
        ? getAliasFromEndpoint(usedEndpoint)
        : plcConfig.alias;

    if (plcConfig.latencyTestEnabled) {
        g_latencyTracker.setEnabled(true);

        if (!plcConfig.latencyCsvPath.empty()) {
            g_latencyTracker.setCsvExport(plcConfig.latencyCsvPath);
        }
    }

    // Load structure once after connection
    readAndStoreNamespaceArray(client, alias);
    for (const auto& nc : plcConfig.nodes) {
        loadNodeStructure(client, nc, alias);
    }

    // One-time method call after connection (if not configured to call before each poll)
    if (plcConfig.methodCallEnabled && !plcConfig.callBeforeEachPoll) {
        callRemoteMethod(client, plcConfig);
    }

    const double intervalSec = std::max<double>(0.001, plcConfig.pollIntervalSec);

    // Connection monitoring variables
    int connectionErrorCount = 0;
    const int MAX_CONSECUTIVE_ERRORS = 3;
    auto lastReconnectAttempt = std::chrono::steady_clock::now();
    const int RECONNECT_COOLDOWN_SEC = 10;

    while (StoppableThread::running) {
        // Check connection state periodically
        if (!isClientConnected(client)) {
            std::cerr << "warn/client        Client connection lost" << std::endl;
            connectionErrorCount++;

            // Prevent too frequent reconnection attempts
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastAttempt = std::chrono::duration_cast<std::chrono::seconds>(
                now - lastReconnectAttempt).count();

            if (timeSinceLastAttempt >= RECONNECT_COOLDOWN_SEC) {
                std::cout << "info/client        Re-establish the connection for the synchronous service call" << std::endl;
                lastReconnectAttempt = now;

                bool reconnected = reconnectClient(client, usedEndpoint,
                    (usedEndpoint == plcConfig.endpoint) ? plcConfig.endpointFallback : plcConfig.endpoint);

                if (reconnected) {
                    std::cout << "info/client        Connection re-established successfully" << std::endl;
                    connectionErrorCount = 0;

                    // Reload namespace array after reconnection
                    readAndStoreNamespaceArray(client, alias);
                } else {
                    std::cerr << "error/client       Failed to re-establish connection" << std::endl;

                    // If reconnection failed after multiple attempts, wait longer
                    if (connectionErrorCount >= MAX_CONSECUTIVE_ERRORS) {
                        std::cerr << "error/client       Too many connection failures, waiting 60s before next attempt" << std::endl;
                        std::this_thread::sleep_for(std::chrono::seconds(60));
                        connectionErrorCount = 0;
                    }
                }
            } else {
                // Wait for cooldown period
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        // Reset error counter when connection is healthy
        connectionErrorCount = 0;

        // Call method before each poll if configured
        if (plcConfig.methodCallEnabled && plcConfig.callBeforeEachPoll) {
            callRemoteMethod(client, plcConfig);
        }

        for (const auto& nc : plcConfig.nodes) {
            updateNodeValue(client, nc, alias);
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(intervalSec));
    }

    UA_Client_disconnect(client);
    UA_Client_delete(client);
    std::cout << "PLC client disconnected from " << usedEndpoint << std::endl;
}

/**
 * Starts the PLC client that queries all defined PLC endpoints from the configuration file.
 */
void runPLCClientConfigurable(const std::string& clientConfigFile) {
    std::vector<PLCConfig> plcConfigs = readClientConfig(clientConfigFile);
    if (plcConfigs.empty()) {
        std::cerr << "No PLC configurations found." << std::endl;
        return;
    }

    std::vector<std::thread> threads;
    for (const auto& plcConfig : plcConfigs) {
        threads.emplace_back(runPLCClientForEndpoint, plcConfig);
    }

    for (auto& thr : threads) {
        thr.join();
    }
}

void runModbusClient(const ModbusConfig& modbusConfig) {
    using json = nlohmann::json;

    // Choose alias: bindToPlc has priority -> values appear in PLC tree
    const std::string aliasForServer =
        !modbusConfig.bindToPlc.empty() ? modbusConfig.bindToPlc
        : (!modbusConfig.alias.empty() ? modbusConfig.alias : "Modbus");

    // Separate Modbus namespace (avoids collision with OPC UA namespace 0)
    static const UA_UInt16 MODBUS_NS = 60000;
    const std::string MODBUS_URI = "urn:Sigma/Modbus/" + aliasForServer;
    g_plcNamespaceMap.set(aliasForServer + ":" + std::to_string(MODBUS_NS), MODBUS_URI);

    // Always create structure, independent of connection status
    namespace fs = std::filesystem;
    fs::path nsFile = getNamespaceJsonPath(aliasForServer, MODBUS_NS);
    fs::create_directories(nsFile.parent_path());

    json nsArray = json::array();
    for (size_t i = 0; i < modbusConfig.registers.size(); ++i) {
        const auto& reg = modbusConfig.registers[i];
        const std::string key = getDataKey(aliasForServer, MODBUS_NS, static_cast<UA_UInt32>(i + 1));

        const char* dtStr = (reg.datatype == "float") ? "11" : "7"; // 11=Float, 7=UInt16 (ns0)

        json nodeStruct{
            {"NamespaceIndex", MODBUS_NS},
            {"NodeID", static_cast<int>(i + 1)},
            {"BrowseName", reg.name},
            {"DisplayName", "Modbus " + reg.name},
            {"DataType", dtStr},
            {"Path", {"Objects", aliasForServer}}
        };
        g_structureMap.set(key, nodeStruct);
        nsArray.push_back({ {"NamespaceIndex", MODBUS_NS}, {"NodeID", static_cast<int>(i + 1)} });

        // Initial value
        if (reg.datatype == "float") g_dataMap.set(key, json{ {"Value", nullptr} });
        else                         g_dataMap.set(key, json{ {"Value", 0} });
    }
    { std::ofstream out(nsFile); if (out.is_open()) out << nsArray.dump(4); }

    std::string host = modbusConfig.ip;
    int port = modbusConfig.port;

    modbus_t* ctx = nullptr;
    const bool isHostname = (host.find_first_not_of("0123456789.") != std::string::npos);
    if (isHostname) {
        char portStr[16]; std::snprintf(portStr, sizeof(portStr), "%d", port);
        ctx = modbus_new_tcp_pi(host.c_str(), portStr);
    }
    else {
        ctx = modbus_new_tcp(host.c_str(), port);
    }

    if (!ctx) {
        std::cerr << "Unable to create modbus context\n";
        return;
    }

    if (modbus_connect(ctx) == -1) {
        std::cerr << "Modbus connection failed for " << host << ":" << port
            << ": " << modbus_strerror(errno) << std::endl;
        modbus_free(ctx);
        return;
    }

    // Polling interval from JSON configuration (seconds), minimum 0.001
    const double intervalSec = std::max<double>(0.001, modbusConfig.pollIntervalSec);

    // Main read loop
    while (StoppableThread::running) {
        for (size_t i = 0; i < modbusConfig.registers.size(); ++i) {
            const auto& reg = modbusConfig.registers[i];
            uint16_t raw = 0;
            if (modbus_read_registers(ctx, reg.address, 1, &raw) != 1) {
                std::cerr << "Modbus read error: " << reg.name << std::endl;
                continue;
            }

            json val;
            if (reg.datatype == "float") val["Value"] = raw / 10.0; // Demo scaling
            else                         val["Value"] = raw;        // uint16

            const std::string key = getDataKey(aliasForServer, MODBUS_NS, static_cast<UA_UInt32>(i + 1));
            g_dataMap.set(key, val);
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(intervalSec));
    }

    modbus_close(ctx);
    modbus_free(ctx);
}