#include "session_manager.h"
#include "hydra_log.h"
#include <sys/socket.h>
#include <cstring>
#include <algorithm>

SessionManager::SessionManager(ganl::NetworkEngine& engine,
                               AccountManager& accounts,
                               const HydraConfig& config)
    : engine_(engine), accounts_(accounts), config_(config) {
}

SessionManager::~SessionManager() {
}

void SessionManager::sendToClient(ganl::ConnectionHandle handle,
                                  const std::string& text) {
    int fd = static_cast<int>(handle);
    send(fd, text.data(), text.size(), MSG_NOSIGNAL);
}

void SessionManager::showBanner(ganl::ConnectionHandle handle) {
    bool bootstrap = accounts_.isEmpty();
    std::string banner = "\r\nHydra v0.1\r\n";
    if (bootstrap) {
        banner += "First connection — create the admin account.\r\n";
        banner += "Or type 'create <username> <password>' to create an account.\r\n";
    }
    banner += "\r\nUsername: ";
    sendToClient(handle, banner);
}

void SessionManager::showGameMenu(HydraSession& session,
                                  ganl::ConnectionHandle fdHandle) {
    std::string menu = "\r\n--- Available Games ---\r\n";
    for (size_t i = 0; i < config_.games.size(); i++) {
        menu += "  " + config_.games[i].name + " ("
              + config_.games[i].host + ":"
              + std::to_string(config_.games[i].port) + ")\r\n";
    }
    menu += "\r\nUse /connect <game> to connect.\r\n";
    if (session.backDoor != ganl::InvalidConnectionHandle) {
        menu += "[Currently connected to " + session.gameName + "]\r\n";
    }
    sendToClient(fdHandle, menu);
}

// ---- Front-door lifecycle ----

void SessionManager::onAccept(ganl::ConnectionHandle handle) {
    FrontDoorState fd;
    fd.handle = handle;
    fd.loginPhase = FrontDoorState::AwaitUsername;
    frontDoors_[handle] = fd;

    LOG_INFO("Session manager: new front-door %lu", (unsigned long)handle);
    showBanner(handle);
}

void SessionManager::onFrontDoorData(ganl::ConnectionHandle handle,
                                     const char* data, size_t len) {
    auto it = frontDoors_.find(handle);
    if (it == frontDoors_.end()) return;

    FrontDoorState& fd = it->second;

    // Assemble lines from raw data (handle \r\n, \n, \r)
    for (size_t i = 0; i < len; i++) {
        char ch = data[i];
        if (ch == '\n') {
            // Strip trailing \r if present
            if (!fd.lineBuf.empty() && fd.lineBuf.back() == '\r') {
                fd.lineBuf.pop_back();
            }
            processLine(fd, fd.lineBuf);
            fd.lineBuf.clear();
        } else if (ch == '\0') {
            // Telnet NUL after CR — ignore
        } else {
            fd.lineBuf += ch;
        }
    }
}

void SessionManager::onFrontDoorClose(ganl::ConnectionHandle handle) {
    auto it = frontDoors_.find(handle);
    if (it == frontDoors_.end()) return;

    FrontDoorState& fd = it->second;
    HydraSessionId sid = fd.sessionId;

    frontDoors_.erase(it);

    if (sid != InvalidHydraSessionId) {
        auto sit = sessions_.find(sid);
        if (sit != sessions_.end()) {
            HydraSession& session = sit->second;
            auto& fds = session.frontDoors;
            fds.erase(std::remove(fds.begin(), fds.end(), handle),
                      fds.end());

            if (fds.empty()) {
                LOG_INFO("Session %lu: all front-doors gone, detaching",
                         (unsigned long)sid);
                session.state = SessionState::Detached;
                // Back-door stays alive — session persists
            }
        }
    }

    LOG_INFO("Front-door %lu closed", (unsigned long)handle);
}

void SessionManager::processLine(FrontDoorState& fd,
                                 const std::string& line) {
    if (fd.loginPhase != FrontDoorState::Authenticated) {
        handleLogin(fd, line);
        return;
    }

    // Authenticated — route to session
    auto sit = sessions_.find(fd.sessionId);
    if (sit == sessions_.end()) return;
    HydraSession& session = sit->second;

    session.lastActivity = time(nullptr);

    // Check for Hydra command prefix
    if (!line.empty() && line[0] == '/') {
        if (line.size() > 1 && line[1] == '/') {
            // Escaped: "//" → send "/" to game
            forwardToGame(session, line.substr(1));
            return;
        }
        dispatchCommand(session, fd.handle, line);
        return;
    }

    // Forward to game
    forwardToGame(session, line);
}

void SessionManager::handleLogin(FrontDoorState& fd,
                                 const std::string& line) {
    switch (fd.loginPhase) {
    case FrontDoorState::AwaitUsername: {
        // Check for "create <username> <password>" command
        if (line.substr(0, 7) == "create " && line.size() > 7) {
            size_t space = line.find(' ', 7);
            if (space != std::string::npos && space + 1 < line.size()) {
                std::string username = line.substr(7, space - 7);
                std::string password = line.substr(space + 1);
                bool admin = accounts_.isEmpty();  // First account is admin

                uint32_t accountId = 0;
                std::string errorMsg;
                if (accounts_.createAccount(username, password, admin,
                                            accountId, errorMsg)) {
                    sendToClient(fd.handle,
                        "Account '" + username + "' created"
                        + (admin ? " (admin)" : "") + ".\r\n"
                        "Logging in...\r\n");

                    // Auto-login after create
                    std::vector<uint8_t> sbKey;
                    uint32_t authId = accounts_.authenticate(
                        username, password, sbKey);
                    if (authId > 0) {
                        fd.pendingUsername = username;
                        fd.loginPhase = FrontDoorState::Authenticated;

                        // Create session
                        HydraSession session;
                        session.id = nextSessionId_++;
                        session.accountId = authId;
                        session.username = username;
                        session.created = time(nullptr);
                        session.lastActivity = session.created;
                        session.scrollbackKey = sbKey;
                        session.frontDoors.push_back(fd.handle);

                        fd.sessionId = session.id;
                        sessions_[session.id] = std::move(session);

                        LOG_INFO("Session %lu created for '%s'",
                                 (unsigned long)fd.sessionId,
                                 username.c_str());

                        showGameMenu(sessions_[fd.sessionId], fd.handle);
                    }
                } else {
                    sendToClient(fd.handle,
                        "Account creation failed: " + errorMsg + "\r\n"
                        "Username: ");
                }
                return;
            }
        }

        fd.pendingUsername = line;
        fd.loginPhase = FrontDoorState::AwaitPassword;
        // Don't echo password
        sendToClient(fd.handle, "Password: ");
    } break;

    case FrontDoorState::AwaitPassword: {
        std::vector<uint8_t> sbKey;
        uint32_t accountId = accounts_.authenticate(
            fd.pendingUsername, line, sbKey);

        if (accountId == 0) {
            LOG_INFO("Login failed for '%s' from fd %lu",
                     fd.pendingUsername.c_str(),
                     (unsigned long)fd.handle);
            sendToClient(fd.handle,
                "\r\nLogin failed.\r\n\r\nUsername: ");
            fd.loginPhase = FrontDoorState::AwaitUsername;
            fd.pendingUsername.clear();
            return;
        }

        fd.loginPhase = FrontDoorState::Authenticated;

        // Check for existing session to resume
        HydraSession* existing = nullptr;
        for (auto& [sid, sess] : sessions_) {
            if (sess.accountId == accountId) {
                existing = &sess;
                break;
            }
        }

        if (existing) {
            // Resume existing session
            existing->frontDoors.push_back(fd.handle);
            existing->state = SessionState::Active;
            existing->lastActivity = time(nullptr);
            fd.sessionId = existing->id;

            LOG_INFO("Session %lu resumed for '%s'",
                     (unsigned long)existing->id,
                     fd.pendingUsername.c_str());

            sendToClient(fd.handle,
                "\r\nResuming session...\r\n");

            // Replay scroll-back
            if (existing->scrollback.count() > 0) {
                sendToClient(fd.handle,
                    "-- Scroll-back ("
                    + std::to_string(existing->scrollback.count())
                    + " lines) --\r\n");

                existing->scrollback.replay(
                    existing->scrollback.count(),
                    [](const std::string& text,
                       const std::string& source,
                       time_t timestamp, void* ctx) {
                        auto* handle = static_cast<ganl::ConnectionHandle*>(ctx);
                        int fd = static_cast<int>(*handle);
                        std::string line = text + "\r\n";
                        send(fd, line.data(), line.size(), MSG_NOSIGNAL);
                    },
                    &fd.handle);

                sendToClient(fd.handle,
                    "-- End scroll-back --\r\n");
            }

            if (existing->backDoor != ganl::InvalidConnectionHandle) {
                sendToClient(fd.handle,
                    "[" + existing->gameName + ": connected]\r\n");
            } else {
                showGameMenu(*existing, fd.handle);
            }
        } else {
            // New session
            HydraSession session;
            session.id = nextSessionId_++;
            session.accountId = accountId;
            session.username = fd.pendingUsername;
            session.created = time(nullptr);
            session.lastActivity = session.created;
            session.scrollbackKey = sbKey;
            session.frontDoors.push_back(fd.handle);

            fd.sessionId = session.id;
            sessions_[session.id] = std::move(session);

            LOG_INFO("Session %lu created for '%s'",
                     (unsigned long)fd.sessionId,
                     fd.pendingUsername.c_str());

            sendToClient(fd.handle, "\r\nWelcome, "
                + fd.pendingUsername + ".\r\n");
            showGameMenu(sessions_[fd.sessionId], fd.handle);
        }
    } break;

    case FrontDoorState::Authenticated:
        // Should not reach here
        break;
    }
}

// ---- Session commands ----

void SessionManager::dispatchCommand(HydraSession& session,
                                     ganl::ConnectionHandle fdHandle,
                                     const std::string& line) {
    // Parse command
    std::string cmd = line.substr(1);  // strip leading /
    size_t space = cmd.find(' ');
    std::string verb = cmd.substr(0, space);
    std::string args = (space != std::string::npos)
        ? cmd.substr(space + 1) : "";

    // Lowercase the verb
    std::transform(verb.begin(), verb.end(), verb.begin(),
        [](unsigned char c) { return std::tolower(c); });

    if (verb == "games") {
        showGameMenu(session, fdHandle);
    } else if (verb == "connect") {
        if (args.empty()) {
            sendToClient(fdHandle, "Usage: /connect <game>\r\n");
            return;
        }
        connectToGame(session, args);
    } else if (verb == "quit") {
        // Close back-door if connected
        if (session.backDoor != ganl::InvalidConnectionHandle) {
            backDoorMap_.erase(session.backDoor);
            engine_.closeConnection(session.backDoor);
            session.backDoor = ganl::InvalidConnectionHandle;
        }
        // Close all front-doors
        for (auto h : session.frontDoors) {
            sendToClient(h, "Session destroyed. Goodbye.\r\n");
            engine_.closeConnection(h);
            frontDoors_.erase(h);
        }
        sessions_.erase(session.id);
    } else if (verb == "detach") {
        sendToClient(fdHandle, "Session detached. Reconnect to resume.\r\n");
        engine_.closeConnection(fdHandle);
        // onFrontDoorClose will handle the rest
    } else if (verb == "scroll") {
        size_t n = 20;
        if (!args.empty()) {
            try { n = std::stoul(args); } catch (...) {}
        }
        if (session.scrollback.count() == 0) {
            sendToClient(fdHandle, "[No scroll-back]\r\n");
        } else {
            sendToClient(fdHandle,
                "-- Last " + std::to_string(n) + " lines --\r\n");
            session.scrollback.replay(n,
                [](const std::string& text,
                   const std::string& source,
                   time_t timestamp, void* ctx) {
                    auto* handle = static_cast<ganl::ConnectionHandle*>(ctx);
                    int fd = static_cast<int>(*handle);
                    std::string line = text + "\r\n";
                    send(fd, line.data(), line.size(), MSG_NOSIGNAL);
                },
                &fdHandle);
            sendToClient(fdHandle, "-- End --\r\n");
        }
    } else if (verb == "links") {
        if (session.backDoor != ganl::InvalidConnectionHandle) {
            sendToClient(fdHandle,
                "  [1] " + session.gameName + " (active)\r\n");
        } else {
            sendToClient(fdHandle, "  No active links.\r\n");
        }
    } else {
        sendToClient(fdHandle,
            "Unknown command: /" + verb + "\r\n"
            "Commands: /games /connect /links /scroll /detach /quit\r\n");
    }
}

void SessionManager::connectToGame(HydraSession& session,
                                   const std::string& gameName) {
    // Find game config
    const GameConfig* game = nullptr;
    for (const auto& g : config_.games) {
        if (g.name == gameName) {
            game = &g;
            break;
        }
    }

    if (!game) {
        for (auto h : session.frontDoors) {
            sendToClient(h, "Unknown game: " + gameName + "\r\n");
        }
        return;
    }

    // Close existing back-door if any
    if (session.backDoor != ganl::InvalidConnectionHandle) {
        backDoorMap_.erase(session.backDoor);
        engine_.closeConnection(session.backDoor);
        session.backDoor = ganl::InvalidConnectionHandle;
    }

    ganl::ErrorCode err = 0;
    ganl::ConnectionHandle bdHandle;

    if (game->transport == GameTransport::Unix) {
        bdHandle = engine_.initiateUnixConnect(
            game->socketPath, nullptr, err);
    } else {
        bdHandle = engine_.initiateConnect(
            game->host, game->port, nullptr, err);
    }

    if (bdHandle == ganl::InvalidConnectionHandle) {
        for (auto h : session.frontDoors) {
            sendToClient(h,
                "[" + game->name + ": connection failed: "
                + strerror(err) + "]\r\n");
        }
        return;
    }

    session.backDoor = bdHandle;
    session.gameName = game->name;
    session.linkState = LinkState::Connecting;
    backDoorMap_[bdHandle] = session.id;

    for (auto h : session.frontDoors) {
        sendToClient(h,
            "[" + game->name + ": connecting...]\r\n");
    }

    LOG_INFO("Session %lu: connecting to %s (%s:%u)",
             (unsigned long)session.id, game->name.c_str(),
             game->host.c_str(), game->port);
}

void SessionManager::forwardToGame(HydraSession& session,
                                   const std::string& line) {
    if (session.backDoor == ganl::InvalidConnectionHandle ||
        session.linkState != LinkState::Active) {
        for (auto h : session.frontDoors) {
            sendToClient(h,
                "[No active game link. Use /connect <game>]\r\n");
        }
        return;
    }

    std::string data = line + "\r\n";
    int bdFd = static_cast<int>(session.backDoor);
    send(bdFd, data.data(), data.size(), MSG_NOSIGNAL);
}

// ---- Back-door lifecycle ----

void SessionManager::onBackDoorConnect(ganl::ConnectionHandle bdHandle) {
    auto it = backDoorMap_.find(bdHandle);
    if (it == backDoorMap_.end()) return;

    auto sit = sessions_.find(it->second);
    if (sit == sessions_.end()) return;

    HydraSession& session = sit->second;
    session.linkState = LinkState::Active;

    LOG_INFO("Session %lu: back-door connected to %s",
             (unsigned long)session.id, session.gameName.c_str());

    for (auto h : session.frontDoors) {
        sendToClient(h,
            "[" + session.gameName + ": connected]\r\n");
    }
}

void SessionManager::onBackDoorConnectFail(ganl::ConnectionHandle bdHandle,
                                           int error) {
    auto it = backDoorMap_.find(bdHandle);
    if (it == backDoorMap_.end()) return;

    auto sit = sessions_.find(it->second);
    if (sit == sessions_.end()) {
        backDoorMap_.erase(it);
        return;
    }

    HydraSession& session = sit->second;
    session.linkState = LinkState::Dead;
    session.backDoor = ganl::InvalidConnectionHandle;
    backDoorMap_.erase(it);

    LOG_ERROR("Session %lu: back-door connect failed: %s",
              (unsigned long)session.id, strerror(error));

    for (auto h : session.frontDoors) {
        sendToClient(h,
            "[" + session.gameName + ": connection failed: "
            + strerror(error) + "]\r\n");
    }
}

void SessionManager::onBackDoorData(ganl::ConnectionHandle bdHandle,
                                    const char* data, size_t len) {
    auto it = backDoorMap_.find(bdHandle);
    if (it == backDoorMap_.end()) return;

    auto sit = sessions_.find(it->second);
    if (sit == sessions_.end()) return;

    HydraSession& session = sit->second;

    // Append to scroll-back (raw for now — PUA conversion in Step 7)
    std::string text(data, len);
    session.scrollback.append(text, session.gameName);

    // Forward to all front-doors
    for (auto h : session.frontDoors) {
        send(static_cast<int>(h), data, len, MSG_NOSIGNAL);
    }
}

void SessionManager::onBackDoorClose(ganl::ConnectionHandle bdHandle) {
    auto it = backDoorMap_.find(bdHandle);
    if (it == backDoorMap_.end()) return;

    auto sit = sessions_.find(it->second);
    if (sit != sessions_.end()) {
        HydraSession& session = sit->second;
        session.linkState = LinkState::Dead;
        session.backDoor = ganl::InvalidConnectionHandle;

        LOG_INFO("Session %lu: back-door lost (%s)",
                 (unsigned long)session.id, session.gameName.c_str());

        for (auto h : session.frontDoors) {
            sendToClient(h,
                "[" + session.gameName + ": disconnected]\r\n");
        }
    }

    backDoorMap_.erase(it);
}

bool SessionManager::isBackDoor(ganl::ConnectionHandle handle) const {
    return backDoorMap_.find(handle) != backDoorMap_.end();
}

HydraSession* SessionManager::findSessionByBackDoor(
    ganl::ConnectionHandle bdHandle) {
    auto it = backDoorMap_.find(bdHandle);
    if (it == backDoorMap_.end()) return nullptr;
    auto sit = sessions_.find(it->second);
    if (sit == sessions_.end()) return nullptr;
    return &sit->second;
}

void SessionManager::runTimers() {
    // TODO: idle timeouts, detached session cleanup, reconnect backoff
}
