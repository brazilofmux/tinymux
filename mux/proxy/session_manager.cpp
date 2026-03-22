#include "session_manager.h"
#include "hydra_log.h"

SessionManager::SessionManager(FrontDoor& frontDoor, BackDoor& backDoor,
                               const HydraConfig& config)
    : frontDoor_(frontDoor), backDoor_(backDoor), config_(config) {
}

SessionManager::~SessionManager() {
    for (auto& [id, session] : sessions_) {
        delete session;
    }
}

void SessionManager::onFrontDoorInput(ganl::ConnectionHandle handle,
                                      const std::string& line) {
    FrontDoorConn* fd = frontDoor_.findConn(handle);
    if (!fd) return;

    if (fd->sessionId == InvalidHydraSessionId) {
        handleLogin(fd, line);
        return;
    }

    // Check for Hydra command prefix
    if (!line.empty() && line[0] == '/') {
        if (line.size() > 1 && line[1] == '/') {
            // Escaped: "//" -> send "/" to game
            // Fall through with leading '/' stripped
        } else {
            auto it = sessions_.find(fd->sessionId);
            if (it != sessions_.end()) {
                dispatchSessionCommand(it->second, handle, line);
            }
            return;
        }
    }

    // Forward to active back-door link
    auto it = sessions_.find(fd->sessionId);
    if (it == sessions_.end()) return;

    HydraSession* session = it->second;
    if (session->activeLink &&
        session->activeLink->state == LinkState::Active) {
        // TODO: charset conversion via telnet bridge
        backDoor_.sendToGame(session->activeLink, line + "\r\n");
    } else {
        sendToFrontDoor(handle,
            "[No active game link. Use /connect <game>]\r\n");
    }
}

void SessionManager::onFrontDoorConnect(ganl::ConnectionHandle handle) {
    LOG_INFO("Front door connect: handle %lu", (unsigned long)handle);
    // TODO: send banner and login prompt
    sendToFrontDoor(handle, "\r\nHydra v0.1\r\nUsername: ");
}

void SessionManager::onFrontDoorDisconnect(ganl::ConnectionHandle handle) {
    LOG_INFO("Front door disconnect: handle %lu", (unsigned long)handle);
    // TODO: detach from session, handle session lifecycle
}

void SessionManager::onBackDoorData(BackDoorLink* link,
                                    const std::string& data) {
    if (!link) return;
    auto it = sessions_.find(link->sessionId);
    if (it == sessions_.end()) return;

    HydraSession* session = it->second;

    // TODO: ingest via telnet bridge (ANSI->PUA), append to scrollback

    // Forward to all attached front-door connections
    for (auto fdHandle : session->frontDoors) {
        // TODO: render via telnet bridge (PUA->ANSI)
        sendToFrontDoor(fdHandle, data);
    }
}

void SessionManager::onBackDoorConnect(BackDoorLink* link) {
    if (!link) return;
    LOG_INFO("Back door connected: %s for session %lu",
        link->gameName.c_str(), (unsigned long)link->sessionId);
}

void SessionManager::onBackDoorDisconnect(BackDoorLink* link) {
    if (!link) return;
    LOG_INFO("Back door disconnected: %s for session %lu",
        link->gameName.c_str(), (unsigned long)link->sessionId);
    // TODO: reconnect logic
}

void SessionManager::runTimers() {
    // TODO: idle timeouts, reconnect backoff, scroll-back flush
}

void SessionManager::handleLogin(FrontDoorConn* fd,
                                 const std::string& line) {
    // TODO: implement login state machine
    (void)fd;
    (void)line;
}

void SessionManager::dispatchSessionCommand(HydraSession* session,
                                            ganl::ConnectionHandle fdHandle,
                                            const std::string& line) {
    // TODO: parse /games, /connect, /switch, /links, /detach,
    //       /scroll, /quit, /passwd, /who
    sendToFrontDoor(fdHandle,
        "[Unknown command: " + line + "]\r\n");
    (void)session;
}

void SessionManager::sendToFrontDoor(ganl::ConnectionHandle handle,
                                     const std::string& text) {
    frontDoor_.sendToClient(handle, text);
}
