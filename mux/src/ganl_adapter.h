#pragma once
#ifndef GANL_ADAPTER_H
#define GANL_ADAPTER_H

#ifdef USE_GANL

#include "autoconf.h"
#include "config.h"
#include "externs.h"

#ifdef notify_all
#undef notify_all
#endif

// Include necessary GANL headers
#include "network_engine_factory.h"
#include "secure_transport_factory.h"
#include "telnet_protocol_handler.h" // Assuming a Telnet handler exists
#include "session_manager.h"
#include "connection.h" // Include ConnectionBase

#include <memory>
#include <map>
#include <mutex>
#include <string>

// Forward declare TinyMUX DESC if not fully included via externs.h
struct descriptor_data;
using DESC = struct descriptor_data;

namespace ganl {
    // Forward declarations for GANL classes used
    class NetworkEngine;
    class SecureTransport;
    class ConnectionBase;
}

class GanlTinyMuxSessionManager;
class GanlTinyMuxProtocolHandler;

class GanlAdapter {
public:
    GanlAdapter();
    ~GanlAdapter();

    // --- Initialization/Shutdown ---
    bool initialize();
    void shutdown();
    void run_main_loop();

    // --- TinyMUX Interface ---
    void send_data(DESC* d, const char* data, size_t len);
    void close_connection(DESC* d, ganl::DisconnectReason reason);
    std::string get_remote_address(DESC* d);
    int get_socket_descriptor(DESC* d); // For legacy functions needing descriptor num

    // --- Mapping ---
    void add_mapping(ganl::ConnectionHandle handle, DESC* d, std::shared_ptr<ganl::ConnectionBase> conn);
    void remove_mapping(DESC* d);
    DESC* get_desc(ganl::ConnectionHandle handle);
    std::shared_ptr<ganl::ConnectionBase> get_connection(DESC* d);
    ganl::ConnectionHandle get_handle(DESC* d);

    // --- Accessors for Callbacks ---
    ganl::NetworkEngine* get_engine() { return networkEngine_.get(); }
    DESC* allocate_desc();
    void free_desc2(DESC* d);

    std::unique_ptr<ganl::NetworkEngine> networkEngine_;
    std::unique_ptr<ganl::SecureTransport> secureTransport_;
    std::unique_ptr<ganl::ProtocolHandler> protocolHandler_; // Changed name
    std::unique_ptr<ganl::SessionManager> sessionManager_;   // Changed name

    bool initialized_ = false;
    std::mutex mutex_; // Protect maps

    // Mappings
    std::map<ganl::ConnectionHandle, DESC*> handle_to_desc_;
    std::map<DESC*, ganl::ConnectionHandle> desc_to_handle_;
    // Store the connection pointer itself for easy access
    std::map<ganl::ConnectionHandle, std::shared_ptr<ganl::ConnectionBase>> handle_to_conn_;

    // Listener Handles (Port -> ListenerHandle)
    std::map<int, ganl::ListenerHandle> port_listeners_;
    std::map<int, ganl::ListenerHandle> ssl_port_listeners_;

    // Store listener context pointers (ListenerHandle -> PortInfo - needed for TLS check?)
    struct ListenerContext {
        int port;
        bool is_ssl;
    };
    std::map<ganl::ListenerHandle, ListenerContext> listener_contexts_;

    void process_tinyMUX_tasks(); // Helper to run timers, quotas etc.
};

// Global adapter instance
extern GanlAdapter g_GanlAdapter;

// Public functions for TinyMUX to call
void ganl_initialize();
void ganl_shutdown();
void ganl_main_loop();
void ganl_send_data_str(DESC* d, const UTF8* data);
void ganl_send_data_mux_string(DESC* d, const mux_string& data);
void ganl_close_connection(DESC* d, int reason); // TinyMUX reason codes
void ganl_associate_player(DESC* d, dbref player); // Needed after successful login

// Refactored legacy Telnet parser prototype
void process_input_from_ganl(DESC* d, const std::string& line);

#endif // USE_GANL
#endif // GANL_ADAPTER_H