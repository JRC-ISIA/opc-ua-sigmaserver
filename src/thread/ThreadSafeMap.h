/**
 * @file ThreadSafeMap.h
 * @brief Thread-safe map container for concurrent access to JSON data
 *
 * Provides a simple wrapper around std::unordered_map with mutex protection,
 * allowing multiple threads to safely read and write JSON values concurrently.
 */

#pragma once
#include <unordered_map>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

/**
 * @class ThreadSafeMap
 * @brief Thread-safe string-to-JSON map for inter-thread communication
 *
 * This class provides a simple key-value store where keys are strings and values
 * are JSON objects. All operations are protected by a mutex to ensure thread safety.
 * Used extensively throughout the application for storing node values, structures,
 * namespace mappings, and pending write operations.
 *
 */
class ThreadSafeMap {
public:
    /**
     * @brief Sets a key-value pair in the map
     *
     * @param key The key string (e.g., "PLC1:ns=2;i=1001")
     * @param value The JSON value to store
     */
    void set(const std::string& key, const nlohmann::json& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_map[key] = value;
    }

    /**
     * @brief Retrieves a value from the map
     *
     * @param key The key to look up
     * @param value Output parameter that receives the JSON value if found
     * @return true if the key was found, false otherwise
     */
    bool get(const std::string& key, nlohmann::json& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            value = it->second;
            return true;
        }
        return false;
    }

    /**
     * @brief Retrieves all key-value pairs as a single JSON object
     *
     * @return JSON object containing all key-value pairs (keys become JSON object keys)
     */
    nlohmann::json getAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        nlohmann::json j;
        for (const auto& pair : m_map) {
            j[pair.first] = pair.second;
        }
        return j;
    }

private:
    std::unordered_map<std::string, nlohmann::json> m_map;  ///< Internal storage for key-value pairs
    std::mutex m_mutex;  ///< Mutex protecting concurrent access to m_map
};
