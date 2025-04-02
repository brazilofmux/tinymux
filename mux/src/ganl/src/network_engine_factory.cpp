#include "network_engine_factory.h"

// Include platform-specific engines conditionally
#if defined(__linux__)
    #include "epoll_network_engine.h"
    #define HAVE_EPOLL 1
#else
    #define HAVE_EPOLL 0
#endif

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #include "kqueue_network_engine.h"
    #define HAVE_KQUEUE 1
#else
    #define HAVE_KQUEUE 0
#endif

#if defined(_WIN32) || defined(WIN32)
    #include "wselect_network_engine.h"
    #define HAVE_WSELECT 1

    // IOCP implementation
    #include "iocp_network_engine.h"
    #define HAVE_IOCP 1
#else
    #include "select_network_engine.h"
    #define HAVE_WSELECT 0
    #define HAVE_IOCP 0
#endif

#include <iostream>
#include <sstream>
#include <memory>

namespace ganl {

std::unique_ptr<NetworkEngine> NetworkEngineFactory::createEngine(NetworkEngineType type) {
    // If Auto type requested, determine the best available
    if (type == NetworkEngineType::Auto) {
        #if HAVE_EPOLL
            type = NetworkEngineType::Epoll;
        #elif HAVE_KQUEUE
            type = NetworkEngineType::Kqueue;
        #elif HAVE_IOCP
            type = NetworkEngineType::IOCP;
        #elif HAVE_WSELECT
            type = NetworkEngineType::WSelect;
        #else
            type = NetworkEngineType::Select;
        #endif

        std::cout << "Auto-selected network engine type: ";
        switch (type) {
            case NetworkEngineType::Epoll: std::cout << "epoll"; break;
            case NetworkEngineType::Kqueue: std::cout << "kqueue"; break;
            case NetworkEngineType::IOCP: std::cout << "IOCP"; break;
            case NetworkEngineType::Select: std::cout << "select"; break;
            case NetworkEngineType::WSelect: std::cout << "Windows select"; break;
            default: std::cout << "unknown"; break;
        }
        std::cout << std::endl;
    }

    // Create the requested engine type
    switch (type) {
        case NetworkEngineType::Select:
#if defined(_WIN32) || defined(WIN32)
            // On Windows, redirect Select to WSelect for better compatibility
            std::cout << "Note: Redirecting Select to WSelect on Windows platform" << std::endl;
            return std::make_unique<WSelectNetworkEngine>();
#else
            return std::make_unique<SelectNetworkEngine>();
#endif

        case NetworkEngineType::WSelect:
#if HAVE_WSELECT
            return std::make_unique<WSelectNetworkEngine>();
#else
            std::cerr << "Error: Windows Select engine requested but not available on this platform." << std::endl;
            return nullptr;
#endif

        case NetworkEngineType::Epoll:
            #if HAVE_EPOLL
                return std::make_unique<EpollNetworkEngine>();
            #else
                std::cerr << "Error: epoll engine requested but not available on this platform." << std::endl;
                return nullptr;
            #endif

        case NetworkEngineType::Kqueue:
            #if HAVE_KQUEUE
                return std::make_unique<KqueueNetworkEngine>();
            #else
                std::cerr << "Error: kqueue engine requested but not available on this platform." << std::endl;
                return nullptr;
            #endif

        case NetworkEngineType::IOCP:
            #if HAVE_IOCP
                return std::make_unique<IocpNetworkEngine>();
            #else
                std::cerr << "Error: IOCP engine requested but not available on this platform." << std::endl;
                return nullptr;
            #endif

        default:
            std::cerr << "Error: Unknown network engine type." << std::endl;
            return nullptr;
    }
}

bool NetworkEngineFactory::isEngineTypeAvailable(NetworkEngineType type) {
        switch (type) {
        case NetworkEngineType::Select:
            return true; // Always available (redirects to WSelect on Windows)

        case NetworkEngineType::WSelect:
            #if HAVE_WSELECT
                    return true;
            #else
                    return false;
            #endif

        case NetworkEngineType::Epoll:
            #if HAVE_EPOLL
                return true;
            #else
                return false;
            #endif

        case NetworkEngineType::Kqueue:
            #if HAVE_KQUEUE
                return true;
            #else
                return false;
            #endif

        case NetworkEngineType::IOCP:
            #if HAVE_IOCP
                return true;
            #else
                return false;
            #endif

        case NetworkEngineType::Auto:
            return true; // Always "available" as it selects a real one

        default:
            return false;
    }
}

std::string NetworkEngineFactory::getAvailableEngineTypes() {
    std::ostringstream oss;
    oss << "Available network engines: ";

    // Select is always available
    oss << "select";

    #if HAVE_WSELECT
        oss << ", wselect";
    #endif

    #if HAVE_EPOLL
        oss << ", epoll";
    #endif

    #if HAVE_KQUEUE
        oss << ", kqueue";
    #endif

    #if HAVE_IOCP
        oss << ", IOCP";
    #endif

    return oss.str();
}

} // namespace ganl
