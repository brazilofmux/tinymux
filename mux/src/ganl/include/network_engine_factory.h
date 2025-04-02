#ifndef GANL_NETWORK_ENGINE_FACTORY_H
#define GANL_NETWORK_ENGINE_FACTORY_H

#include "network_engine.h"
#include <memory>
#include <string>

namespace ganl {

// Forward declarations to avoid including implementation headers
class SelectNetworkEngine;

/**
 * Supported network engine types
 */
enum class NetworkEngineType {
    Auto,       // Automatically choose the best available
    Select,     // Use select() (portable Unix, lower performance)
    WSelect,    // Use Windows select() (Windows-specific implementation)
    Epoll,      // Use epoll() (Linux only)
    Kqueue,     // Use kqueue() (BSD, macOS)
    IOCP        // Use I/O Completion Ports (Windows only)
};

/**
 * Factory for creating network engine instances
 */
class NetworkEngineFactory {
public:
    /**
     * Create a network engine of the specified type
     *
     * @param type Engine type to create
     * @return Unique pointer to the created engine, or nullptr if not available
     */
    static std::unique_ptr<NetworkEngine> createEngine(NetworkEngineType type = NetworkEngineType::Auto);

    /**
     * Check if a specific engine type is available on this platform
     *
     * @param type Engine type to check
     * @return true if available, false otherwise
     */
    static bool isEngineTypeAvailable(NetworkEngineType type);

    /**
     * Get a list of available engine types on this platform
     *
     * @return String describing available engines
     */
    static std::string getAvailableEngineTypes();
};

} // namespace ganl

#endif // GANL_NETWORK_ENGINE_FACTORY_H
