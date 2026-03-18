// main.cpp -- Entry point and IOCP event loop for the Win32 console MU* client.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>

#include "app.h"
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
        if (!app.fg->is_connected()) {
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
struct ConsoleThreadData {
    HANDLE hIn;
    HANDLE iocp;
};

static DWORD WINAPI ConsoleInputThread(LPVOID param) {
    auto* data = (ConsoleThreadData*)param;
    INPUT_RECORD records[16];
    DWORD count;

    for (;;) {
        if (!ReadConsoleInputW(data->hIn, records, 16, &count)) break;
        for (DWORD i = 0; i < count; i++) {
            // Allocate a copy to post via IOCP
            auto* rec = new INPUT_RECORD(records[i]);
            PostQueuedCompletionStatus(data->iocp, sizeof(INPUT_RECORD),
                                       IOCP_KEY_CONSOLE, (LPOVERLAPPED)rec);
        }
    }
    return 0;
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

    // Start console input thread
    ConsoleThreadData ct_data = { GetStdHandle(STD_INPUT_HANDLE), app.iocp };
    HANDLE hThread = CreateThread(nullptr, 0, ConsoleInputThread, &ct_data, 0, nullptr);

    // Main IOCP event loop
    while (app.running) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;

        // Wait with 100ms timeout for prompt detection
        BOOL ok = GetQueuedCompletionStatus(app.iocp, &bytes, &key, &overlapped, 100);

        if (ok && key == IOCP_KEY_CONSOLE && overlapped) {
            // Console input event
            auto* rec = (INPUT_RECORD*)overlapped;
            InputEvent ev = app.terminal.translate(*rec);
            delete rec;

            if (ev.type != InputEvent::None) {
                // Check keybindings first
                BindKey bk = event_to_bindkey(ev);
                const std::string* bound_cmd = app.keybindings.find(bk);
                if (bound_cmd) {
                    if ((*bound_cmd)[0] == '/') {
                        app.commands.dispatch(app, *bound_cmd);
                    } else if (app.fg) {
                        app_send_line(app, app.fg, *bound_cmd);
                    }
                } else {
                    std::string line;
                    if (app.terminal.handle_key(ev, line)) {
                        // User pressed Enter
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
        } else if (ok && overlapped) {
            // Network I/O completion
            Connection* conn = (Connection*)key;
            IoContext* ctx = CONTAINING_RECORD(overlapped, IoContext, overlapped);
            auto lines = conn->on_completion(ctx, bytes, 0);
            for (auto& line : lines) {
                app_receive_line(app, conn, conn->world_name(), line);
            }

            if (!conn->is_connected()) {
                app.terminal.print_system("Connection lost: " + conn->world_name());
                if (app.fg == conn) app.fg = nullptr;
                app.connections.erase(conn->world_name());
                if (!app.fg && !app.connections.empty()) {
                    app.fg = app.connections.begin()->second.get();
                    app.terminal.set_output_context(app.fg->world_name());
                    app.terminal.set_history_context(app.fg->world_name());
                }
            }
        } else if (!ok && overlapped) {
            // Failed I/O
            Connection* conn = (Connection*)key;
            IoContext* ctx = CONTAINING_RECORD(overlapped, IoContext, overlapped);
            DWORD err = GetLastError();
            conn->on_completion(ctx, 0, err);

            if (!conn->is_connected()) {
                app.terminal.print_system("Connection failed: " + conn->world_name());
                if (app.fg == conn) app.fg = nullptr;
                app.connections.erase(conn->world_name());
            }
        }

        // Check for prompts on all connections
        for (auto& [name, conn] : app.connections) {
            std::string prompt = conn->check_prompt(500);
            if (!prompt.empty()) {
                app_receive_line(app, conn.get(), name, prompt);
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

    // Cleanup
    app.terminal.shutdown();

    // Disconnect all
    app.connections.clear();

    if (hThread != INVALID_HANDLE_VALUE) {
        TerminateThread(hThread, 0);
        CloseHandle(hThread);
    }
    CloseHandle(app.iocp);
    WSACleanup();

    return 0;
}
