/**
 * @file StoppableThread.h
 * @brief Wrapper for std::thread with coordinated shutdown capability
 *
 * Provides a thread wrapper that supports graceful shutdown via a shared atomic flag.
 * All threads can be stopped simultaneously by setting the static running flag to false,
 * allowing for clean application shutdown without abrupt termination.
 */

#pragma once
#include <thread>
#include <atomic>
#include <functional>

/**
 * @class StoppableThread
 * @brief Thread wrapper with coordinated shutdown support
 *
 * Manages the lifecycle of a worker thread with graceful shutdown capabilities.
 * Uses a static atomic flag shared by all instances, allowing coordinated shutdown
 * of all threads in the application (e.g., on SIGINT/SIGTERM).
 *
 */
class StoppableThread {
public:
    /// Default constructor
    StoppableThread() = default;

    // @brief Starts the thread with the given function
    template<typename Function>
    void start(Function&& func) {
        m_thread = std::thread([this, func]() {
            running = true;
            func();
            });
    }


    // @brief Signals the thread to stop
    void stop() {
        running = false;
    }

    // @briefWaits for the thread to complete
    void join() {
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    // @brief Shared atomic flag controlling all thread lifecycles
    static std::atomic_bool running;

private:
    std::thread m_thread;  ///< The underlying std::thread object
};

/// Initialize the static running flag to true (threads run by default)
inline std::atomic_bool StoppableThread::running = true;