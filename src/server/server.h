/**
 * @file server.h
 * @brief OPC UA server interface for exposing aggregated data
 *
 * Provides the main function for running an OPC UA server that exposes data
 * from the global data maps. Supports both Client-Server and PubSub modes,
 * with optional security and latency tracking.
 */

#pragma once
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include "config.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <csignal>

/**
 * @brief Runs an OPC UA server instance
 *
 * Starts an OPC UA server on the specified port and dynamically creates variable nodes
 * based on the global structure map (g_structureMap). The server continuously reads
 * values from g_dataMap and exposes them to external OPC UA clients.
 *
 *
 * @param port TCP port for the OPC UA server (e.g., 4840, 4841)
 * @param plcAlias PLC alias whose data should be exposed by this server
 * @param mode Server operational mode: "CS" (Client-Server) or "PubSub"
 * @param latencyTestEnabled Enable latency tracking for t2 measurement (default: false)
 */
void runServer(UA_UInt16 port, const std::string& plcAlias, const std::string& mode, bool latencyTestEnabled = false);