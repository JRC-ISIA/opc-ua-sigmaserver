/**
 * @file SharedData.h
 * @brief Global shared data structures for inter-thread communication
 *
 * This file declares the global thread-safe data structures used for communication
 * between OPC UA clients, Modbus clients, and OPC UA servers. All data structures
 * are thread-safe and can be accessed concurrently from multiple threads.
 */

#pragma once
#include "ThreadSafeMap.h"

// Forward declaration
class LatencyTracker;

/**
 * @brief Global thread-safe maps for data exchange between components
 *
 * These maps provide the core data storage mechanism for the aggregation server:
 */
extern ThreadSafeMap g_dataMap;          ///< Current node values from all sources
extern ThreadSafeMap g_structureMap;     ///< Node metadata and hierarchy information
extern ThreadSafeMap g_plcNamespaceMap;  ///< PLC namespace URI mappings
extern ThreadSafeMap g_writeMap;         ///< Pending write operations queue

/**
 * @brief Global latency tracker for performance monitoring
 *
 * Tracks two latency measurements:
 * - t1: Time from client reading a value to storing it in g_dataMap
 * - t2: Time from g_dataMap update to server exposing the value to external clients
 *
 * Enables end-to-end performance analysis of the aggregation pipeline.
 */
extern LatencyTracker g_latencyTracker;