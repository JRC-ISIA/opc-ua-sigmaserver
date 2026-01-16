/**
 * @file SharedData.cpp
 * @brief Implementation file for global shared data structures
 *
 * Defines the global instances of thread-safe data structures declared in SharedData.h.
 * These objects are initialized at program startup and persist throughout execution.
 */

#include "SharedData.h"
#include "LatencyTracking.h"

// Global thread-safe map instances for inter-component communication
ThreadSafeMap g_dataMap;          ///< Stores current values from all data sources
ThreadSafeMap g_structureMap;     ///< Stores node metadata and hierarchy
ThreadSafeMap g_plcNamespaceMap;  ///< Maps PLC aliases to namespace URIs
ThreadSafeMap g_writeMap;         ///< Queue for pending write operations

/// Global latency tracker instance for performance monitoring
LatencyTracker g_latencyTracker;