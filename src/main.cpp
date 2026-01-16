/**
 * @file main.cpp
 * @brief Entry point for the OPC UA Aggregation Server application
 *
 * This application aggregates data from multiple OPC UA PLCs and Modbus devices,
 * then exposes the aggregated data through OPC UA server endpoints. It manages
 * multiple client and server threads with graceful shutdown capabilities.
 * 
 */

#include "server.h"
#include "client.h"
#include "config.h"
#include "StoppableThread.h"
#include <vector>
#include <csignal>

/// Global vector storing all active threads for lifecycle management
std::vector<StoppableThread> threads;

/**
 * @brief Signal handler for graceful shutdown on SIGINT/SIGTERM
 *
 * Stops all running threads and sets the global running flag to false,
 * allowing the application to shut down cleanly without data corruption.
 *
 * @param sig Signal number (SIGINT or SIGTERM)
 */
void signalHandler(int sig) {
    std::cout << "\nStopping threads..." << std::endl;
    for (auto& t : threads) {
        t.stop();
    }
    StoppableThread::running = false;
}

/**
 * @brief Main entry point for the aggregation server application
 *
 * @return EXIT_SUCCESS on clean shutdown, or error code on failure
 */
int main() {
    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Phase 1: Start OPC UA client threads to connect to PLCs and load node structures
    std::vector<PLCConfig> plcConfigs = readClientConfig("client_configuration.json");
    for (const auto& plc : plcConfigs) {
        StoppableThread t;
        t.start([plc]() { runPLCClientForEndpoint(plc); });
        threads.push_back(std::move(t));
    }

    // Wait for OPC UA clients to discover and populate node structures
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Phase 2: Start Modbus client threads to connect to Modbus TCP devices
    std::vector<ModbusConfig> modbusConfigs = readModbusConfig("client_configuration.json");
    for (const auto& mb : modbusConfigs) {
        StoppableThread t;
        t.start([mb]() { runModbusClient(mb); });
        threads.push_back(std::move(t));
    }

    // Wait for Modbus clients to initialize connections and start reading registers
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Phase 3: Start OPC UA server threads to expose aggregated data
    std::vector<ServerConfig> serverConfigs = readServerConfig("server_configuration.json");
    for (const auto& sc : serverConfigs) {
        StoppableThread t;
        // Start server with configuration (port, PLC alias, mode, latency tracking)
        t.start([sc]() { runServer(sc.port, sc.plcAlias, sc.mode, sc.latencyTestEnabled); });
        threads.push_back(std::move(t));
    }

    // Wait for all threads to complete (runs until SIGINT/SIGTERM received)
    for (auto& t : threads) {
        t.join();
    }

    return EXIT_SUCCESS;
}