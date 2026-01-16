/**
 * @file LatencyTracking.h
 * @brief Latency tracking system for the aggregation server
 *
 * Measures the time from packet arrival at the client to packet departure at the server.
 * Tracks two key metrics:
 * - t1: Client read to data store write time
 * - t2: Data store read to server response time
 */
#pragma once

#include <chrono>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <string>
#include <ctime>
#include <nlohmann/json.hpp>

using TimePoint = std::chrono::high_resolution_clock::time_point;
using Duration = std::chrono::duration<double, std::micro>;  // Microseconds

/**
 * @brief Latency statistics for a single node
 */
struct LatencyStats {
    double t1_us = 0.0;          // t1 in microseconds (Client → DataStore)
    double t2_us = 0.0;          // t2 in microseconds (DataStore → Server)
    double total_us = 0.0;       // Total latency in microseconds
    TimePoint t1_start;          // Start time for t1 measurement
    TimePoint t2_start;          // Start time for t2 measurement
    size_t sample_count = 0;     // Number of measurements
    double avg_t1_us = 0.0;      // Average t1
    double avg_t2_us = 0.0;      // Average t2
    double avg_total_us = 0.0;   // Average total
};

/**
 * @brief Global latency tracker (Thread-Safe)
 */
class LatencyTracker {
private:
    std::unordered_map<std::string, LatencyStats> stats_;
    mutable std::mutex mutex_;
    bool enabled_ = false;
    std::string csv_file_path_;
    bool csv_enabled_ = false;
    bool csv_header_written_ = false;

    void writeCsvHeader() {
        if (csv_header_written_ || csv_file_path_.empty()) return;

        std::ofstream csv_file(csv_file_path_, std::ios::out);
        if (csv_file.is_open()) {
            csv_file << "timestamp,key,t1_us,t2_us,total_us,total_ms,sample_count,avg_t1_us,avg_t2_us,avg_total_us,avg_total_ms\n";
            csv_file.close();
            csv_header_written_ = true;
            std::cout << "[LatencyTracking] CSV file created: " << csv_file_path_ << std::endl;
        } else {
            std::cerr << "[LatencyTracking] ERROR: Could not create CSV file: " << csv_file_path_ << std::endl;
        }
    }

    void writeCsvLine(const std::string& key, const LatencyStats& s) {
        if (!csv_enabled_ || csv_file_path_.empty()) return;

        auto now = std::chrono::system_clock::now();
        auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        std::ofstream csv_file(csv_file_path_, std::ios::app);
        if (csv_file.is_open()) {
            csv_file << std::fixed << std::setprecision(2);
            csv_file << timestamp_ms << ","
                     << key << ","
                     << s.t1_us << ","
                     << s.t2_us << ","
                     << s.total_us << ","
                     << (s.total_us / 1000.0) << ","
                     << s.sample_count << ","
                     << s.avg_t1_us << ","
                     << s.avg_t2_us << ","
                     << s.avg_total_us << ","
                     << (s.avg_total_us / 1000.0) << "\n";
            csv_file.close();
        }
    }

public:
    // Enables/disables latency tracking
    void setEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = enabled;
        if (enabled) {
            std::cout << "\n[LatencyTracking] ✓ Latency measurement ENABLED" << std::endl;
        }
    }

    // Enables CSV export with specified file path
    void setCsvExport(const std::string& csv_path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!csv_path.empty()) {
            csv_file_path_ = csv_path;
            csv_enabled_ = true;
            csv_header_written_ = false;
            writeCsvHeader();
            std::cout << "[LatencyTracking] ✓ CSV export ENABLED: " << csv_path << std::endl;
        }
    }

    bool isEnabled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return enabled_;
    }

    // Starts t1 measurement (Client receives packet)
    void startT1(const std::string& key) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        stats_[key].t1_start = std::chrono::high_resolution_clock::now();
    }

    // Stops t1 measurement (Value is stored in DataStore)
    void stopT1(const std::string& key) {
        if (!enabled_) return;
        auto t1_end = std::chrono::high_resolution_clock::now();

        std::lock_guard<std::mutex> lock(mutex_);
        auto& s = stats_[key];
        Duration t1_duration = t1_end - s.t1_start;
        s.t1_us = t1_duration.count();

        // Calculate average
        s.avg_t1_us = (s.avg_t1_us * s.sample_count + s.t1_us) / (s.sample_count + 1);
    }

    // Starts t2 measurement (Server queries value)
    void startT2(const std::string& key) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        stats_[key].t2_start = std::chrono::high_resolution_clock::now();
    }

    // Stops t2 measurement (Value leaves server) and calculates total latency
    void stopT2(const std::string& key) {
        if (!enabled_) return;
        auto t2_end = std::chrono::high_resolution_clock::now();

        std::lock_guard<std::mutex> lock(mutex_);
        auto& s = stats_[key];
        Duration t2_duration = t2_end - s.t2_start;
        s.t2_us = t2_duration.count();

        // Total latency = t1 + t2
        s.total_us = s.t1_us + s.t2_us;

        // Calculate averages
        s.avg_t2_us = (s.avg_t2_us * s.sample_count + s.t2_us) / (s.sample_count + 1);
        s.avg_total_us = (s.avg_total_us * s.sample_count + s.total_us) / (s.sample_count + 1);
        s.sample_count++;

        // Write CSV line (on every measurement)
        writeCsvLine(key, s);

        // Print output (every 100 samples)
        if (s.sample_count % 100 == 0) {
            printStats(key, s);
        }
    }

    // Prints statistics for a key
    void printStats(const std::string& key, const LatencyStats& s) const {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\n[LatencyTracking] Key: " << key << std::endl;
        std::cout << "  Current: t1=" << s.t1_us << " μs, t2=" << s.t2_us
                  << " μs, total=" << s.total_us << " μs" << std::endl;
        std::cout << "  Average (n=" << s.sample_count << "): t1=" << s.avg_t1_us
                  << " μs, t2=" << s.avg_t2_us << " μs, total=" << s.avg_total_us
                  << " μs (" << (s.avg_total_us / 1000.0) << " ms)" << std::endl;
    }

    // Prints all statistics
    void printAllStats() const {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "\n========================================" << std::endl;
        std::cout << "LATENCY STATISTICS SUMMARY" << std::endl;
        std::cout << "========================================" << std::endl;
        for (const auto& [key, s] : stats_) {
            if (s.sample_count > 0) {
                printStats(key, s);
            }
        }
        std::cout << "========================================\n" << std::endl;
    }

    // Exports statistics as JSON
    nlohmann::json exportToJson() const {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json j = nlohmann::json::array();

        for (const auto& [key, s] : stats_) {
            if (s.sample_count > 0) {
                nlohmann::json entry;
                entry["key"] = key;
                entry["samples"] = s.sample_count;
                entry["avg_t1_us"] = s.avg_t1_us;
                entry["avg_t2_us"] = s.avg_t2_us;
                entry["avg_total_us"] = s.avg_total_us;
                entry["avg_total_ms"] = s.avg_total_us / 1000.0;
                entry["last_t1_us"] = s.t1_us;
                entry["last_t2_us"] = s.t2_us;
                entry["last_total_us"] = s.total_us;
                j.push_back(entry);
            }
        }

        return j;
    }
};

// Global instance
extern LatencyTracker g_latencyTracker;
