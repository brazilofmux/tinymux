// main.cpp -- Entry point and IOCP event loop for the Win32 console MU* client.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>

#include "app.h"
#ifdef HYDRA_GRPC
#include "hydra_connection.h"
#endif
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

// Build a status bar string from current app state.
static void update_status_bar(App& app) {
    std::string status;
    if (app.fg) {
        status = app.fg->world_name();
        if (app.fg->uses_ssl()) status += " [ssl]";
        auto* hydra = dynamic_cast<HydraConnection*>(app.fg);
        if (hydra && hydra->is_reconnecting()) {
            status += "  reconnecting";
        } else if (!app.fg->is_connected()) {
            status += " (disconnected)";
        } else {
            int idle = app.fg->idle_secs();
            if (idle > 0) {
                status += "  idle:" + std::to_string(idle) + "s";
            }
        }
        if (app.fg->is_logging()) status += "  [LOG]";
    } else {
        status = "(no connection)";
    }
    int nconn = (int)app.connections.size();
    int nactive = (int)app.active_worlds.size();
    if (nconn > 1) {
        status += "  [" + std::to_string(nconn) + " conn";
        if (nactive > 0) status += ", " + std::to_string(nactive) + " active";
        status += "]";
    } else if (nactive > 0) {
        status += "  [" + std::to_string(nactive) + " active]";
    }
    app.terminal.set_status(status);
}

// Console input reader thread.  Reads INPUT_RECORDs and posts them to the IOCP.
// Exits cleanly when hShutdown is signaled via CancelSynchronousIo.
struct ConsoleThreadData {
    HANDLE hIn;
    HANDLE iocp;
    volatile bool* shutdown;
};

static DWORD WINAPI ConsoleInputThread(LPVOID param) {
    auto* data = (ConsoleThreadData*)param;
    INPUT_RECORD records[16];
    DWORD count;

    while (!*data->shutdown) {
        if (!ReadConsoleInputW(data->hIn, records, 16, &count)) break;
        for (DWORD i = 0; i < count; i++) {
            auto* rec = new INPUT_RECORD(records[i]);
            PostQueuedCompletionStatus(data->iocp, sizeof(INPUT_RECORD),
                                       IOCP_KEY_CONSOLE, (LPOVERLAPPED)rec);
        }
    }
    return 0;
}

// Remove a dead connection safely (outside iteration).
static void remove_connection(App& app, const std::string& name, const char* reason) {
    app.terminal.print_system(std::string(reason) + ": " + name);
    auto it = app.connections.find(name);
    if (it != app.connections.end()) {
        if (app.fg == it->second.get()) app.fg = nullptr;
        app.connections.erase(it);
    }
    app.hydra_line_buffers.erase(name);
    app.active_worlds.erase(name);
    app_clear_partial_line(app, name);
    if (!app.fg && !app.connections.empty()) {
        app.fg = app.connections.begin()->second.get();
        app.terminal.set_output_context(app.fg->world_name());
        app.terminal.set_history_context(app.fg->world_name());
    }
}

int main(int argc, char* argv[]) {
    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }

    App app;

    // Create the IOCP
    app.iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (app.iocp == nullptr) {
        fprintf(stderr, "Failed to create IOCP.\n");
        WSACleanup();
        return 1;
    }

    // Initialize terminal
    if (!app.terminal.init()) {
        fprintf(stderr, "Failed to initialize terminal.\n");
        CloseHandle(app.iocp);
        WSACleanup();
        return 1;
    }

    // Register commands
    register_builtin_commands(app);

    // Load worlds file if present
    app.worlddb.load("worlds.txt");

    app.terminal.print_system("TinyMUX Console Client");
    app.terminal.print_system("Type /help for commands, /connect <host> <port> to connect.");

    // Start console input thread with clean shutdown flag
    volatile bool input_shutdown = false;
    ConsoleThreadData ct_data = { GetStdHandle(STD_INPUT_HANDLE), app.iocp, &input_shutdown };
    DWORD thread_id = 0;
    HANDLE hThread = CreateThread(nullptr, 0, ConsoleInputThread, &ct_data, 0, &thread_id);

    // Main IOCP event loop
    while (app.running) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL ok = GetQueuedCompletionStatus(app.iocp, &bytes, &key, &overlapped, 100);

        // Collect dead connections for deferred removal (avoids iterator invalidation)
        std::vector<std::string> to_remove;

        if (ok && key == IOCP_KEY_CONSOLE && overlapped) {
            // Console input event
            auto* rec = (INPUT_RECORD*)overlapped;
            InputEvent ev = app.terminal.translate(*rec);
            delete rec;

            if (ev.type != InputEvent::None) {
                BindKey bk = event_to_bindkey(ev);
                const std::string* bound_cmd = app.keybindings.find(bk);
                if (bound_cmd) {
                    if (!bound_cmd->empty() && (*bound_cmd)[0] == '/') {
                        app.commands.dispatch(app, *bound_cmd);
                    } else if (app.fg) {
                        app_send_line(app, app.fg, *bound_cmd);
                    }
                } else {
                    std::string line;
                    if (app.terminal.handle_key(ev, line)) {
                        if (!line.empty() && line[0] == '/') {
                            app.commands.dispatch(app, line);
                        } else if (app.fg) {
                            app_send_line(app, app.fg, line);
                        } else if (!line.empty()) {
                            app.terminal.print_system("Not connected. Use /connect.");
                        }
                    }
                }
            }
        } else if (ok && key == IOCP_KEY_HYDRA) {
#ifdef HYDRA_GRPC
            // Hydra gRPC output ready — drain all Hydra connections
            for (auto& [name, conn] : app.connections) {
                auto* hydra = dynamic_cast<HydraConnection*>(conn.get());
                if (!hydra) continue;
                auto chunks = hydra->drain_output_chunks();
                for (auto& chunk : chunks) {
                    app_receive_hydra_chunk(app, hydra, name, chunk.text,
                                            chunk.is_stream_text,
                                            chunk.end_of_record);
                }
                if (!hydra->is_connected() && !hydra->is_reconnecting()) {
                    to_remove.push_back(name);
                }
            }
#endif
        } else if (ok && overlapped) {
            // Telnet network I/O completion
            Connection* conn = (Connection*)(void*)key;
            IoContext* ctx = CONTAINING_RECORD(overlapped, IoContext, overlapped);
            auto lines = conn->on_completion(ctx, bytes, 0);
            for (auto& line : lines) {
                app_receive_line(app, conn, conn->world_name(), line);
            }
            if (!conn->is_connected()) {
                to_remove.push_back(conn->world_name());
            }
        } else if (!ok && overlapped) {
            // Failed telnet I/O
            Connection* conn = (Connection*)(void*)key;
            IoContext* ctx = CONTAINING_RECORD(overlapped, IoContext, overlapped);
            DWORD err = GetLastError();
            conn->on_completion(ctx, 0, err);
            if (!conn->is_connected()) {
                to_remove.push_back(conn->world_name());
            }
        }

        // Deferred connection removal (safe — not inside connection iteration)
        for (auto& name : to_remove) {
            remove_connection(app, name, "Connection lost");
        }

        // Check for prompts (iterate by index to tolerate removal)
        {
            auto names = std::vector<std::string>();
            for (auto& [name, conn] : app.connections) names.push_back(name);
            for (auto& name : names) {
                auto it = app.connections.find(name);
                if (it == app.connections.end()) continue;
                std::string prompt = it->second->check_prompt(500);
                if (!prompt.empty()) {
                    app_receive_partial_line(app, it->second.get(), name, prompt);
                } else if (!it->second->has_partial_line()) {
                    app_clear_partial_line(app, name);
                }
            }
        }

        // Fire timers
        auto timer_cmds = app.timers.check_and_fire();
        for (auto& cmd : timer_cmds) {
            if (!cmd.empty() && cmd[0] == '/') {
                app.commands.dispatch(app, cmd);
            } else if (app.fg) {
                app_send_line(app, app.fg, cmd);
            }
        }

        // Update status bar
        update_status_bar(app);
    }

    // Cleanup: signal input thread to exit gracefully
    input_shutdown = true;
    if (hThread != INVALID_HANDLE_VALUE) {
        // Cancel the blocking ReadConsoleInputW
        CancelSynchronousIo(hThread);
        // Wait briefly for clean exit, then allow process shutdown to reap it.
        WaitForSingleObject(hThread, 2000);
        CloseHandle(hThread);
    }

    app.terminal.shutdown();
    app.connections.clear();
    CloseHandle(app.iocp);
    WSACleanup();

    return 0;
}
