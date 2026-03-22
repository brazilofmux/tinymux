#include "session_manager.h"
#include "crypto.h"
#include "hydra_log.h"
#include <sys/socket.h>
#include <cstring>
#include <algorithm>

// Context for scroll-back replay callback — renders PUA through the bridge.
struct ReplayContext {
    ganl::ConnectionHandle handle;
    ganl::EncodingType encoding;
    ColorDepth colorDepth;
    TelnetBridge* bridge;
};

// Telnet constants for GMCP parsing
static constexpr unsigned char T_IAC = 255;
static constexpr unsigned char T_SB  = 250;
static constexpr unsigned char T_SE  = 240;
static constexpr unsigned char T_GMCP = 201;
static constexpr unsigned char T_WILL = 251;
static constexpr unsigned char T_DO   = 253;

// A GMCP sub-negotiation extracted from the raw byte stream.
struct GmcpMessage {
    std::string payload;  // everything between SB GMCP and IAC SE
};

// Split raw telnet data into regular bytes and GMCP sub-negotiations.
// Regular bytes go into 'regular', GMCP messages go into 'gmcp'.
// Also detects WILL GMCP / DO GMCP for capability tracking.
static void splitGmcp(const char* data, size_t len,
                      std::string& regular,
                      std::vector<GmcpMessage>& gmcp,
                      bool& sawWillGmcp, bool& sawDoGmcp) {
    sawWillGmcp = false;
    sawDoGmcp = false;
    regular.reserve(len);

    enum { Normal, SawIAC, SawSB, InGmcpSB, InGmcpIAC, SawCmd } state = Normal;
    unsigned char cmdByte = 0;
    std::string gmcpBuf;

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = static_cast<unsigned char>(data[i]);

        switch (state) {
        case Normal:
            if (ch == T_IAC) { state = SawIAC; }
            else { regular.push_back(static_cast<char>(ch)); }
            break;

        case SawIAC:
            if (ch == T_IAC) {
                regular.push_back(static_cast<char>(T_IAC));  // escaped IAC
                state = Normal;
            } else if (ch == T_SB) {
                state = SawSB;
            } else if (ch == T_WILL || ch == T_DO) {
                cmdByte = ch;
                state = SawCmd;
            } else {
                // Other telnet command — pass through
                regular.push_back(static_cast<char>(T_IAC));
                regular.push_back(static_cast<char>(ch));
                state = Normal;
            }
            break;

        case SawCmd:
            if (ch == T_GMCP) {
                if (cmdByte == T_WILL) sawWillGmcp = true;
                if (cmdByte == T_DO)   sawDoGmcp = true;
            }
            // Pass the negotiation through to regular stream
            regular.push_back(static_cast<char>(T_IAC));
            regular.push_back(static_cast<char>(cmdByte));
            regular.push_back(static_cast<char>(ch));
            state = Normal;
            break;

        case SawSB:
            if (ch == T_GMCP) {
                gmcpBuf.clear();
                state = InGmcpSB;
            } else {
                // Non-GMCP subneg — pass through
                regular.push_back(static_cast<char>(T_IAC));
                regular.push_back(static_cast<char>(T_SB));
                regular.push_back(static_cast<char>(ch));
                state = Normal;  // will catch IAC SE from regular flow
            }
            break;

        case InGmcpSB:
            if (ch == T_IAC) { state = InGmcpIAC; }
            else { gmcpBuf.push_back(static_cast<char>(ch)); }
            break;

        case InGmcpIAC:
            if (ch == T_SE) {
                gmcp.push_back({gmcpBuf});
                state = Normal;
            } else if (ch == T_IAC) {
                gmcpBuf.push_back(static_cast<char>(T_IAC));
                state = InGmcpSB;
            } else {
                // Unexpected byte after IAC inside GMCP SB
                gmcpBuf.push_back(static_cast<char>(T_IAC));
                gmcpBuf.push_back(static_cast<char>(ch));
                state = InGmcpSB;
            }
            break;
        }
    }
}

// Build a GMCP telnet sub-negotiation frame: IAC SB GMCP <payload> IAC SE
static std::string buildGmcpFrame(const std::string& payload) {
    std::string frame;
    frame.reserve(payload.size() + 5);
    frame.push_back(static_cast<char>(T_IAC));
    frame.push_back(static_cast<char>(T_SB));
    frame.push_back(static_cast<char>(T_GMCP));
    frame.append(payload);
    frame.push_back(static_cast<char>(T_IAC));
    frame.push_back(static_cast<char>(T_SE));
    return frame;
}

static const char* linkStateName(LinkState s) {
    switch (s) {
    case LinkState::Connecting:     return "connecting";
    case LinkState::TlsHandshaking: return "tls";
    case LinkState::Negotiating:    return "negotiating";
    case LinkState::AutoLoggingIn:  return "logging-in";
    case LinkState::Active:         return "active";
    case LinkState::Reconnecting:   return "reconnecting";
    case LinkState::Suspended:      return "suspended";
    case LinkState::Dead:           return "dead";
    }
    return "?";
}

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

    // Check if this is a WebSocket connection
    auto it = frontDoors_.find(handle);
    if (it != frontDoors_.end() && it->second.proto == FrontDoorProto::WebSocket) {
        std::string frame = wsEncodeFrame(text);
        send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
        return;
    }

    send(fd, text.data(), text.size(), MSG_NOSIGNAL);
}

void SessionManager::showBanner(ganl::ConnectionHandle handle) {
    bool bootstrap = accounts_.isEmpty();
    std::string banner = "\r\nHydra v0.2\r\n";
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
    if (!session.links.empty()) {
        BackDoorLink* active = session.getActiveLink();
        if (active && active->state == LinkState::Active) {
            menu += "[Active link: " + active->gameName + "]\r\n";
        }
    }
    sendToClient(fdHandle, menu);
}

// ---- Reverse map helper ----

bool SessionManager::findByBackDoor(ganl::ConnectionHandle bdHandle,
                                    HydraSession*& session,
                                    BackDoorLink*& link,
                                    size_t& linkIdx) {
    auto it = backDoorMap_.find(bdHandle);
    if (it == backDoorMap_.end()) return false;

    auto sit = sessions_.find(it->second.sessionId);
    if (sit == sessions_.end()) return false;

    session = &sit->second;
    linkIdx = it->second.linkIndex;
    if (linkIdx >= session->links.size()) return false;
    link = &session->links[linkIdx];
    return true;
}

// ---- Front-door lifecycle ----

void SessionManager::onAccept(ganl::ConnectionHandle handle) {
    FrontDoorState fd;
    fd.handle = handle;
    fd.loginPhase = FrontDoorState::AwaitUsername;
    fd.proto = FrontDoorProto::Telnet;
    frontDoors_[handle] = fd;

    LOG_INFO("Session manager: new front-door %lu (telnet)", (unsigned long)handle);
    showBanner(handle);
}

void SessionManager::onAcceptWebSocket(ganl::ConnectionHandle handle) {
    FrontDoorState fd;
    fd.handle = handle;
    fd.loginPhase = FrontDoorState::AwaitUsername;
    fd.proto = FrontDoorProto::WebSocket;
    // WebSocket clients are always UTF-8 and typically support 256 colors
    fd.encoding = ganl::EncodingType::Utf8;
    fd.colorDepth = ColorDepth::Ansi256;
    frontDoors_[handle] = fd;

    LOG_INFO("Session manager: new front-door %lu (websocket, awaiting handshake)",
             (unsigned long)handle);
    // Don't send banner yet — wait for WebSocket handshake to complete
}

void SessionManager::onFrontDoorData(ganl::ConnectionHandle handle,
                                     const char* data, size_t len) {
    auto it = frontDoors_.find(handle);
    if (it == frontDoors_.end()) return;

    FrontDoorState& fd = it->second;

    // ---- WebSocket path ----
    if (fd.proto == FrontDoorProto::WebSocket) {
        if (!fd.wsState.handshakeComplete) {
            // Still in HTTP upgrade handshake
            std::string response = wsProcessHandshake(fd.wsState, data, len);
            if (!response.empty()) {
                int sockfd = static_cast<int>(handle);
                send(sockfd, response.data(), response.size(), MSG_NOSIGNAL);

                if (!fd.wsState.handshakeOk) {
                    // Handshake failed — close
                    engine_.closeConnection(handle);
                    return;
                }

                LOG_INFO("WebSocket handshake complete for fd %lu",
                         (unsigned long)handle);
                showBanner(handle);

                // Process any trailing data after the handshake
                if (!fd.wsState.handshakeBuf.empty()) {
                    std::string trailing = fd.wsState.handshakeBuf;
                    fd.wsState.handshakeBuf.clear();
                    std::string responses;
                    auto msgs = wsDecodeFrames(fd.wsState,
                        trailing.data(), trailing.size(), responses);
                    if (!responses.empty()) {
                        send(static_cast<int>(handle), responses.data(),
                             responses.size(), MSG_NOSIGNAL);
                    }
                    for (const auto& msg : msgs) {
                        if (msg.opcode == WS_OP_TEXT) {
                            // Process as lines
                            for (char ch : msg.payload) {
                                if (ch == '\n') {
                                    if (!fd.lineBuf.empty() && fd.lineBuf.back() == '\r')
                                        fd.lineBuf.pop_back();
                                    processLine(fd, fd.lineBuf);
                                    fd.lineBuf.clear();
                                } else {
                                    fd.lineBuf += ch;
                                }
                            }
                            // WS messages often don't end with \n
                            if (!fd.lineBuf.empty()) {
                                processLine(fd, fd.lineBuf);
                                fd.lineBuf.clear();
                            }
                        }
                    }
                }
            }
            return;
        }

        // Decode WebSocket frames
        std::string responses;
        auto msgs = wsDecodeFrames(fd.wsState, data, len, responses);
        if (!responses.empty()) {
            send(static_cast<int>(handle), responses.data(),
                 responses.size(), MSG_NOSIGNAL);
        }
        for (const auto& msg : msgs) {
            if (msg.opcode == WS_OP_CLOSE) {
                engine_.closeConnection(handle);
                return;
            }
            if (msg.opcode == WS_OP_TEXT) {
                // Each WS text message is one line (or contains newlines)
                for (char ch : msg.payload) {
                    if (ch == '\n') {
                        if (!fd.lineBuf.empty() && fd.lineBuf.back() == '\r')
                            fd.lineBuf.pop_back();
                        processLine(fd, fd.lineBuf);
                        fd.lineBuf.clear();
                    } else {
                        fd.lineBuf += ch;
                    }
                }
                if (!fd.lineBuf.empty()) {
                    processLine(fd, fd.lineBuf);
                    fd.lineBuf.clear();
                }
            }
        }
        return;
    }

    // ---- Telnet path ----

    // Split GMCP sub-negotiations from regular data
    std::string regular;
    std::vector<GmcpMessage> gmcpMsgs;
    bool sawWillGmcp = false, sawDoGmcp = false;
    splitGmcp(data, len, regular, gmcpMsgs, sawWillGmcp, sawDoGmcp);

    // Track client GMCP capability
    if (sawWillGmcp || sawDoGmcp) {
        fd.gmcpEnabled = true;
    }

    // Forward client GMCP to the active back-door link
    if (!gmcpMsgs.empty() && fd.sessionId != InvalidHydraSessionId) {
        auto sit = sessions_.find(fd.sessionId);
        if (sit != sessions_.end()) {
            BackDoorLink* active = sit->second.getActiveLink();
            if (active && active->handle != ganl::InvalidConnectionHandle &&
                active->gmcpEnabled) {
                for (const auto& gm : gmcpMsgs) {
                    std::string frame = buildGmcpFrame(gm.payload);
                    send(static_cast<int>(active->handle),
                         frame.data(), frame.size(), MSG_NOSIGNAL);
                }
            }
        }
    }

    // Process regular (non-GMCP) data as lines
    for (size_t i = 0; i < regular.size(); i++) {
        char ch = regular[i];
        if (ch == '\n') {
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
                flushSession(session);
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

    auto sit = sessions_.find(fd.sessionId);
    if (sit == sessions_.end()) return;
    HydraSession& session = sit->second;

    session.lastActivity = time(nullptr);

    if (!line.empty() && line[0] == '/') {
        if (line.size() > 1 && line[1] == '/') {
            forwardToGame(session, fd.handle, line.substr(1));
            return;
        }
        dispatchCommand(session, fd.handle, line);
        return;
    }

    forwardToGame(session, fd.handle, line);
}

void SessionManager::handleLogin(FrontDoorState& fd,
                                 const std::string& line) {
    switch (fd.loginPhase) {
    case FrontDoorState::AwaitUsername: {
        if (line.substr(0, 7) == "create " && line.size() > 7) {
            size_t space = line.find(' ', 7);
            if (space != std::string::npos && space + 1 < line.size()) {
                std::string username = line.substr(7, space - 7);
                std::string password = line.substr(space + 1);
                bool admin = accounts_.isEmpty();

                uint32_t accountId = 0;
                std::string errorMsg;
                if (accounts_.createAccount(username, password, admin,
                                            accountId, errorMsg)) {
                    sendToClient(fd.handle,
                        "Account '" + username + "' created"
                        + (admin ? " (admin)" : "") + ".\r\n"
                        "Logging in...\r\n");

                    std::vector<uint8_t> sbKey;
                    uint32_t authId = accounts_.authenticate(
                        username, password, sbKey);
                    if (authId > 0) {
                        fd.pendingUsername = username;
                        fd.loginPhase = FrontDoorState::Authenticated;

                        HydraSession session;
                        session.id = nextSessionId_++;
                        session.accountId = authId;
                        session.username = username;
                        session.created = time(nullptr);
                        session.lastActivity = session.created;
                        session.scrollbackKey = sbKey;
                        session.persistId = generatePersistId();
                        session.frontDoors.push_back(fd.handle);

                        fd.sessionId = session.id;
                        sessions_[session.id] = std::move(session);

                        LOG_INFO("Session %lu (%s) created for '%s'",
                                 (unsigned long)fd.sessionId,
                                 sessions_[fd.sessionId].persistId.c_str(),
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

        // Check for existing in-memory session
        HydraSession* existing = nullptr;
        for (auto& [sid, sess] : sessions_) {
            if (sess.accountId == accountId) {
                existing = &sess;
                break;
            }
        }

        if (existing) {
            existing->frontDoors.push_back(fd.handle);
            existing->state = SessionState::Active;
            existing->lastActivity = time(nullptr);
            fd.sessionId = existing->id;

            LOG_INFO("Session %lu resumed for '%s'",
                     (unsigned long)existing->id,
                     fd.pendingUsername.c_str());

            sendToClient(fd.handle, "\r\nResuming session...\r\n");

            if (existing->scrollback.count() > 0) {
                sendToClient(fd.handle,
                    "-- Scroll-back ("
                    + std::to_string(existing->scrollback.count())
                    + " lines) --\r\n");

                ReplayContext rctx{fd.handle, fd.encoding,
                                   fd.colorDepth, &bridge_};
                existing->scrollback.replay(
                    existing->scrollback.count(),
                    [](const std::string& text,
                       const std::string& source,
                       time_t timestamp, void* ctx) {
                        auto* rc = static_cast<ReplayContext*>(ctx);
                        std::string rendered = rc->bridge->renderForClient(
                            rc->encoding, rc->colorDepth, text);
                        rendered += "\r\n";
                        int fd = static_cast<int>(rc->handle);
                        send(fd, rendered.data(), rendered.size(),
                             MSG_NOSIGNAL);
                    },
                    &rctx);

                sendToClient(fd.handle, "-- End scroll-back --\r\n");
            }

            if (!existing->links.empty()) {
                // Show link status
                for (size_t i = 0; i < existing->links.size(); i++) {
                    const auto& lnk = existing->links[i];
                    if (lnk.state == LinkState::Active ||
                        lnk.state == LinkState::Connecting) {
                        std::string marker = (i == existing->activeLink) ? "*" : " ";
                        sendToClient(fd.handle,
                            "[" + marker + std::to_string(i + 1) + "] "
                            + lnk.gameName + ": " + linkStateName(lnk.state)
                            + "\r\n");
                    }
                }
            } else {
                showGameMenu(*existing, fd.handle);
            }
        } else {
            // Check for saved session in SQLite (after restart)
            AccountManager::SavedSession saved;
            if (accounts_.loadSession(accountId, saved)) {
                resumeSavedSession(fd, accountId, sbKey);
                return;
            }

            HydraSession session;
            session.id = nextSessionId_++;
            session.accountId = accountId;
            session.username = fd.pendingUsername;
            session.created = time(nullptr);
            session.lastActivity = session.created;
            session.scrollbackKey = sbKey;
            session.persistId = generatePersistId();
            session.frontDoors.push_back(fd.handle);

            fd.sessionId = session.id;
            sessions_[session.id] = std::move(session);

            LOG_INFO("Session %lu (%s) created for '%s'",
                     (unsigned long)fd.sessionId,
                     sessions_[fd.sessionId].persistId.c_str(),
                     fd.pendingUsername.c_str());

            sendToClient(fd.handle, "\r\nWelcome, "
                + fd.pendingUsername + ".\r\n");
            showGameMenu(sessions_[fd.sessionId], fd.handle);
        }
    } break;

    case FrontDoorState::Authenticated:
        break;
    }
}

// ---- Session commands ----

void SessionManager::dispatchCommand(HydraSession& session,
                                     ganl::ConnectionHandle fdHandle,
                                     const std::string& line) {
    std::string cmd = line.substr(1);
    size_t space = cmd.find(' ');
    std::string verb = cmd.substr(0, space);
    std::string args = (space != std::string::npos)
        ? cmd.substr(space + 1) : "";

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
    } else if (verb == "switch") {
        if (args.empty()) {
            sendToClient(fdHandle, "Usage: /switch <link#>\r\n");
            return;
        }
        size_t idx = 0;
        try { idx = std::stoul(args); } catch (...) {}
        if (idx < 1 || idx > session.links.size()) {
            sendToClient(fdHandle, "Invalid link number. Use /links to see available links.\r\n");
            return;
        }
        session.activeLink = idx - 1;
        BackDoorLink& lnk = session.links[session.activeLink];
        sendToClient(fdHandle,
            "Switched to link " + std::to_string(idx) + " ("
            + lnk.gameName + ": " + linkStateName(lnk.state) + ")\r\n");
    } else if (verb == "disconnect") {
        if (args.empty()) {
            sendToClient(fdHandle, "Usage: /disconnect <link#>\r\n");
            return;
        }
        size_t idx = 0;
        try { idx = std::stoul(args); } catch (...) {}
        if (idx < 1 || idx > session.links.size()) {
            sendToClient(fdHandle, "Invalid link number.\r\n");
            return;
        }
        size_t li = idx - 1;
        closeLink(session, li);
        sendToClient(fdHandle,
            "Link " + std::to_string(idx) + " disconnected.\r\n");
    } else if (verb == "quit") {
        // Close all links
        for (size_t i = 0; i < session.links.size(); i++) {
            closeLink(session, i);
        }
        // Delete persisted session and scroll-back
        if (!session.persistId.empty()) {
            accounts_.deleteSession(session.persistId);
        }
        for (auto h : session.frontDoors) {
            sendToClient(h, "Session destroyed. Goodbye.\r\n");
            engine_.closeConnection(h);
            frontDoors_.erase(h);
        }
        sessions_.erase(session.id);
    } else if (verb == "detach") {
        sendToClient(fdHandle, "Session detached. Reconnect to resume.\r\n");
        engine_.closeConnection(fdHandle);
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
            auto fdIt = frontDoors_.find(fdHandle);
            ganl::EncodingType enc = ganl::EncodingType::Utf8;
            ColorDepth depth = ColorDepth::Ansi256;
            if (fdIt != frontDoors_.end()) {
                enc = fdIt->second.encoding;
                depth = fdIt->second.colorDepth;
            }
            ReplayContext rctx{fdHandle, enc, depth, &bridge_};
            session.scrollback.replay(n,
                [](const std::string& text,
                   const std::string& source,
                   time_t timestamp, void* ctx) {
                    auto* rc = static_cast<ReplayContext*>(ctx);
                    std::string rendered = rc->bridge->renderForClient(
                        rc->encoding, rc->colorDepth, text);
                    rendered += "\r\n";
                    int fd = static_cast<int>(rc->handle);
                    send(fd, rendered.data(), rendered.size(),
                         MSG_NOSIGNAL);
                },
                &rctx);
            sendToClient(fdHandle, "-- End --\r\n");
        }
    } else if (verb == "links") {
        if (session.links.empty()) {
            sendToClient(fdHandle, "  No links.\r\n");
        } else {
            std::string out;
            for (size_t i = 0; i < session.links.size(); i++) {
                const BackDoorLink& lnk = session.links[i];
                std::string marker = (i == session.activeLink) ? "*" : " ";
                out += "  [" + marker + std::to_string(i + 1) + "] "
                     + lnk.gameName;
                if (!lnk.character.empty()) {
                    out += " (" + lnk.character + ")";
                }
                out += " — " + std::string(linkStateName(lnk.state));
                if (lnk.state == LinkState::Reconnecting) {
                    time_t now = time(nullptr);
                    if (lnk.nextRetry > now) {
                        out += " (retry in "
                             + std::to_string(lnk.nextRetry - now) + "s)";
                    }
                }
                out += "\r\n";
            }
            sendToClient(fdHandle, out);
        }
    } else if (verb == "addcred") {
        std::vector<std::string> parts;
        size_t pos = 0;
        std::string tmp = args;
        while (parts.size() < 4 && (pos = tmp.find(' ')) != std::string::npos) {
            parts.push_back(tmp.substr(0, pos));
            tmp = tmp.substr(pos + 1);
        }
        if (!tmp.empty()) parts.push_back(tmp);

        if (parts.size() < 5) {
            sendToClient(fdHandle,
                "Usage: /addcred <game> <character> <verb> <name> <secret>\r\n"
                "Example: /addcred mygame player1 connect player1 mypassword\r\n");
            return;
        }

        std::string errorMsg;
        if (accounts_.storeCredential(session.accountId,
                                      parts[0], parts[1], parts[2],
                                      parts[3], parts[4], errorMsg)) {
            sendToClient(fdHandle,
                "Credential stored for " + parts[0] + "/" + parts[1] + "\r\n");
        } else {
            sendToClient(fdHandle, "Failed: " + errorMsg + "\r\n");
        }
    } else if (verb == "delcred") {
        if (args.empty()) {
            sendToClient(fdHandle, "Usage: /delcred <game> [character]\r\n");
            return;
        }
        std::string game = args;
        std::string character;
        size_t sp = args.find(' ');
        if (sp != std::string::npos) {
            game = args.substr(0, sp);
            character = args.substr(sp + 1);
        }
        if (accounts_.deleteCredential(session.accountId, game, character)) {
            sendToClient(fdHandle, "Credential(s) deleted.\r\n");
        } else {
            sendToClient(fdHandle, "Delete failed.\r\n");
        }
    } else if (verb == "creds") {
        auto creds = accounts_.listCredentials(session.accountId);
        if (creds.empty()) {
            sendToClient(fdHandle, "No stored credentials.\r\n");
        } else {
            std::string out = "--- Stored Credentials ---\r\n";
            for (const auto& c : creds) {
                out += "  " + c.game + "/" + c.character
                     + "  verb=" + c.verb + " name=" + c.name
                     + (c.autoLogin ? " [auto]" : "") + "\r\n";
            }
            sendToClient(fdHandle, out);
        }
    } else if (verb == "start") {
        if (args.empty()) {
            sendToClient(fdHandle, "Usage: /start <game>\r\n");
            return;
        }
        const GameConfig* game = nullptr;
        for (const auto& g : config_.games) {
            if (g.name == args) { game = &g; break; }
        }
        if (!game) {
            sendToClient(fdHandle, "Unknown game: " + args + "\r\n");
        } else if (game->type != GameType::Local) {
            sendToClient(fdHandle, args + " is a remote game (cannot start).\r\n");
        } else {
            std::string errorMsg;
            if (procMgr_.startGame(*game, errorMsg)) {
                sendToClient(fdHandle,
                    "Started " + args + " (pid "
                    + std::to_string(procMgr_.getPid(args)) + ")\r\n");
            } else {
                sendToClient(fdHandle, "Start failed: " + errorMsg + "\r\n");
            }
        }
    } else if (verb == "stop") {
        if (args.empty()) {
            sendToClient(fdHandle, "Usage: /stop <game>\r\n");
            return;
        }
        if (procMgr_.stopGame(args)) {
            sendToClient(fdHandle, "Stopping " + args + "...\r\n");
        } else {
            sendToClient(fdHandle, args + " is not running.\r\n");
        }
    } else if (verb == "restart") {
        if (args.empty()) {
            sendToClient(fdHandle, "Usage: /restart <game>\r\n");
            return;
        }
        const GameConfig* game = nullptr;
        for (const auto& g : config_.games) {
            if (g.name == args) { game = &g; break; }
        }
        if (!game) {
            sendToClient(fdHandle, "Unknown game: " + args + "\r\n");
        } else if (game->type != GameType::Local) {
            sendToClient(fdHandle, args + " is a remote game.\r\n");
        } else {
            std::string errorMsg;
            if (procMgr_.restartGame(*game, errorMsg)) {
                sendToClient(fdHandle,
                    "Restarted " + args + " (pid "
                    + std::to_string(procMgr_.getPid(args)) + ")\r\n");
            } else {
                sendToClient(fdHandle, "Restart failed: " + errorMsg + "\r\n");
            }
        }
    } else {
        sendToClient(fdHandle,
            "Unknown command: /" + verb + "\r\n"
            "Commands: /games /connect /switch /disconnect /links\r\n"
            "          /scroll /detach /quit\r\n"
            "          /start /stop /restart\r\n"
            "          /addcred /delcred /creds\r\n");
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

    // Check max links limit
    if (session.links.size() >= static_cast<size_t>(config_.maxLinksPerSession)) {
        for (auto h : session.frontDoors) {
            sendToClient(h, "Maximum links reached ("
                + std::to_string(config_.maxLinksPerSession) + ").\r\n");
        }
        return;
    }

    // Autostart local game if configured and not running
    if (game->type == GameType::Local && game->autostart &&
        !procMgr_.isRunning(game->name)) {
        std::string startErr;
        if (procMgr_.startGame(*game, startErr)) {
            LOG_INFO("Autostarted game '%s'", game->name.c_str());
            for (auto h : session.frontDoors) {
                sendToClient(h,
                    "[" + game->name + ": autostarted (pid "
                    + std::to_string(procMgr_.getPid(game->name)) + ")]\r\n");
            }
        } else {
            LOG_WARN("Autostart failed for '%s': %s",
                     game->name.c_str(), startErr.c_str());
        }
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

    // Create new link
    BackDoorLink link;
    link.handle = bdHandle;
    link.gameName = game->name;
    link.state = LinkState::Connecting;
    link.protoState.encoding = game->charset;
    link.gameConfig = game;

    size_t linkIdx = session.links.size();
    session.links.push_back(std::move(link));
    session.activeLink = linkIdx;  // new link becomes active

    backDoorMap_[bdHandle] = {session.id, linkIdx};

    for (auto h : session.frontDoors) {
        sendToClient(h,
            "[" + game->name + ": connecting... (link "
            + std::to_string(linkIdx + 1) + ")]\r\n");
    }

    LOG_INFO("Session %lu: connecting link %zu to %s (%s:%u)",
             (unsigned long)session.id, linkIdx + 1,
             game->name.c_str(), game->host.c_str(), game->port);
}

void SessionManager::forwardToGame(HydraSession& session,
                                   ganl::ConnectionHandle fdHandle,
                                   const std::string& line) {
    BackDoorLink* active = session.getActiveLink();
    if (!active || active->handle == ganl::InvalidConnectionHandle ||
        active->state != LinkState::Active) {
        for (auto h : session.frontDoors) {
            sendToClient(h,
                "[No active game link. Use /connect <game>]\r\n");
        }
        return;
    }

    ganl::EncodingType clientEnc = ganl::EncodingType::Utf8;
    auto fdIt = frontDoors_.find(fdHandle);
    if (fdIt != frontDoors_.end()) {
        clientEnc = fdIt->second.encoding;
    }

    std::string converted = bridge_.convertInput(
        clientEnc, active->protoState.encoding, line);
    converted += "\r\n";

    int bdFd = static_cast<int>(active->handle);
    send(bdFd, converted.data(), converted.size(), MSG_NOSIGNAL);
}

void SessionManager::closeLink(HydraSession& session, size_t linkIdx) {
    if (linkIdx >= session.links.size()) return;
    BackDoorLink& link = session.links[linkIdx];

    if (link.handle != ganl::InvalidConnectionHandle) {
        backDoorMap_.erase(link.handle);
        engine_.closeConnection(link.handle);
        link.handle = ganl::InvalidConnectionHandle;
    }
    link.state = LinkState::Dead;
    link.retryCount = 0;
    link.nextRetry = 0;
}

// ---- Back-door lifecycle ----

void SessionManager::onBackDoorConnect(ganl::ConnectionHandle bdHandle) {
    HydraSession* session = nullptr;
    BackDoorLink* link = nullptr;
    size_t linkIdx = 0;
    if (!findByBackDoor(bdHandle, session, link, linkIdx)) return;

    link->state = LinkState::Active;
    link->retryCount = 0;

    LOG_INFO("Session %lu: link %zu connected to %s",
             (unsigned long)session->id, linkIdx + 1,
             link->gameName.c_str());

    for (auto h : session->frontDoors) {
        sendToClient(h,
            "[" + link->gameName + " (link "
            + std::to_string(linkIdx + 1) + "): connected]\r\n");
    }

    // Auto-login
    std::string verb, loginName, secret;
    if (accounts_.getLoginSecret(session->accountId, link->gameName,
                                 verb, loginName, secret)) {
        std::string loginCmd = verb + " " + loginName + " " + secret + "\r\n";
        int bdFd = static_cast<int>(link->handle);
        send(bdFd, loginCmd.data(), loginCmd.size(), MSG_NOSIGNAL);

        link->character = loginName;

        LOG_INFO("Session %lu: auto-login sent for link %zu (%s)",
                 (unsigned long)session->id, linkIdx + 1,
                 link->gameName.c_str());

        for (auto h : session->frontDoors) {
            sendToClient(h,
                "[" + link->gameName + ": auto-login sent]\r\n");
        }
    }
}

void SessionManager::onBackDoorConnectFail(ganl::ConnectionHandle bdHandle,
                                           int error) {
    HydraSession* session = nullptr;
    BackDoorLink* link = nullptr;
    size_t linkIdx = 0;
    if (!findByBackDoor(bdHandle, session, link, linkIdx)) {
        backDoorMap_.erase(bdHandle);
        return;
    }

    link->state = LinkState::Dead;
    link->handle = ganl::InvalidConnectionHandle;
    backDoorMap_.erase(bdHandle);

    LOG_ERROR("Session %lu: link %zu connect failed: %s",
              (unsigned long)session->id, linkIdx + 1, strerror(error));

    for (auto h : session->frontDoors) {
        sendToClient(h,
            "[" + link->gameName + " (link "
            + std::to_string(linkIdx + 1) + "): connection failed: "
            + strerror(error) + "]\r\n");
    }
}

void SessionManager::onBackDoorData(ganl::ConnectionHandle bdHandle,
                                    const char* data, size_t len) {
    HydraSession* session = nullptr;
    BackDoorLink* link = nullptr;
    size_t linkIdx = 0;
    if (!findByBackDoor(bdHandle, session, link, linkIdx)) return;

    // Split GMCP sub-negotiations from regular data
    std::string regular;
    std::vector<GmcpMessage> gmcpMsgs;
    bool sawWillGmcp = false, sawDoGmcp = false;
    splitGmcp(data, len, regular, gmcpMsgs, sawWillGmcp, sawDoGmcp);

    // Track GMCP capability on the back-door link
    if (sawWillGmcp || sawDoGmcp) {
        link->gmcpEnabled = true;
    }

    // Forward GMCP messages to all GMCP-enabled front-doors
    for (const auto& gm : gmcpMsgs) {
        std::string frame = buildGmcpFrame(gm.payload);
        for (auto h : session->frontDoors) {
            auto fdIt = frontDoors_.find(h);
            if (fdIt == frontDoors_.end()) continue;
            if (!fdIt->second.gmcpEnabled) continue;
            send(static_cast<int>(h), frame.data(), frame.size(),
                 MSG_NOSIGNAL);
        }
    }

    // Process regular (non-GMCP) data through the color/charset bridge
    if (!regular.empty()) {
        std::string puaText = bridge_.ingestGameOutput(
            link->protoState, regular.data(), regular.size());

        session->scrollback.append(puaText, link->gameName);

        for (auto h : session->frontDoors) {
            auto fdIt = frontDoors_.find(h);
            if (fdIt == frontDoors_.end()) continue;
            const FrontDoorState& fd = fdIt->second;

            std::string rendered = bridge_.renderForClient(
                fd.encoding, fd.colorDepth, puaText);

            if (fd.proto == FrontDoorProto::WebSocket) {
                std::string frame = wsEncodeFrame(rendered);
                send(static_cast<int>(h), frame.data(), frame.size(),
                     MSG_NOSIGNAL);
            } else {
                send(static_cast<int>(h), rendered.data(), rendered.size(),
                     MSG_NOSIGNAL);
            }
        }
    }
}

void SessionManager::onBackDoorClose(ganl::ConnectionHandle bdHandle) {
    HydraSession* session = nullptr;
    BackDoorLink* link = nullptr;
    size_t linkIdx = 0;
    if (!findByBackDoor(bdHandle, session, link, linkIdx)) {
        backDoorMap_.erase(bdHandle);
        return;
    }

    backDoorMap_.erase(bdHandle);
    link->handle = ganl::InvalidConnectionHandle;

    // Check for reconnect
    if (link->gameConfig && link->gameConfig->reconnect &&
        (link->state == LinkState::Active ||
         link->state == LinkState::Reconnecting)) {

        const auto& schedule = link->gameConfig->retrySchedule;
        if (link->retryCount < static_cast<int>(schedule.size())) {
            int delay = schedule[static_cast<size_t>(link->retryCount)];
            link->state = LinkState::Reconnecting;
            link->nextRetry = time(nullptr) + delay;
            link->retryCount++;

            LOG_INFO("Session %lu: link %zu (%s) lost, reconnecting in %ds",
                     (unsigned long)session->id, linkIdx + 1,
                     link->gameName.c_str(), delay);

            for (auto h : session->frontDoors) {
                sendToClient(h,
                    "[" + link->gameName + ": disconnected, retrying in "
                    + std::to_string(delay) + "s]\r\n");
            }
            return;
        }
    }

    link->state = LinkState::Dead;

    LOG_INFO("Session %lu: link %zu (%s) lost",
             (unsigned long)session->id, linkIdx + 1,
             link->gameName.c_str());

    for (auto h : session->frontDoors) {
        sendToClient(h,
            "[" + link->gameName + " (link "
            + std::to_string(linkIdx + 1) + "): disconnected]\r\n");
    }
}

bool SessionManager::isBackDoor(ganl::ConnectionHandle handle) const {
    return backDoorMap_.find(handle) != backDoorMap_.end();
}

void SessionManager::runTimers() {
    time_t now = time(nullptr);

    // Periodic scroll-back flush every 60 seconds
    if (now - lastFlush_ >= 60) {
        lastFlush_ = now;
        for (auto& [sid, session] : sessions_) {
            if (session.scrollback.dirtyCount() > 0) {
                flushSession(session);
            }
        }
    }

    // Reap child processes
    procMgr_.reapChildren();

    // Reconnect backoff
    for (auto& [sid, session] : sessions_) {
        for (size_t i = 0; i < session.links.size(); i++) {
            BackDoorLink& link = session.links[i];
            if (link.state != LinkState::Reconnecting) continue;
            if (now < link.nextRetry) continue;

            // Attempt reconnect
            if (!link.gameConfig) {
                link.state = LinkState::Dead;
                continue;
            }

            const GameConfig* game = link.gameConfig;
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
                // Schedule next retry or give up
                const auto& schedule = game->retrySchedule;
                if (link.retryCount < static_cast<int>(schedule.size())) {
                    int delay = schedule[static_cast<size_t>(link.retryCount)];
                    link.nextRetry = now + delay;
                    link.retryCount++;

                    LOG_INFO("Session %lu: link %zu reconnect failed, retry in %ds",
                             (unsigned long)sid, i + 1, delay);
                } else {
                    link.state = LinkState::Dead;
                    LOG_INFO("Session %lu: link %zu reconnect exhausted",
                             (unsigned long)sid, i + 1);

                    for (auto h : session.frontDoors) {
                        sendToClient(h,
                            "[" + link.gameName + " (link "
                            + std::to_string(i + 1)
                            + "): reconnect failed, giving up]\r\n");
                    }
                }
                continue;
            }

            link.handle = bdHandle;
            link.state = LinkState::Connecting;
            backDoorMap_[bdHandle] = {sid, i};

            LOG_INFO("Session %lu: link %zu reconnecting to %s",
                     (unsigned long)sid, i + 1, link.gameName.c_str());

            for (auto h : session.frontDoors) {
                sendToClient(h,
                    "[" + link.gameName + " (link "
                    + std::to_string(i + 1) + "): reconnecting...]\r\n");
            }
        }
    }
}

// ---- Persistence helpers ----

std::string SessionManager::generatePersistId() {
    uint8_t buf[16];
    randomBytes(buf, sizeof(buf));
    static const char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (int i = 0; i < 16; i++) {
        id.push_back(hex[buf[i] >> 4]);
        id.push_back(hex[buf[i] & 0x0F]);
    }
    return id;
}

void SessionManager::flushSession(HydraSession& session) {
    if (session.persistId.empty() || session.scrollbackKey.empty()) return;

    std::string created = std::to_string(session.created);
    std::string lastActive = std::to_string(session.lastActivity);

    // Serialize links as JSON array
    std::string linksJson = "[";
    bool first = true;
    for (const auto& link : session.links) {
        if (link.state == LinkState::Dead) continue;
        if (!first) linksJson += ",";
        linksJson += "{\"game\":\"" + link.gameName + "\"}";
        first = false;
    }
    linksJson += "]";

    std::string errorMsg;
    if (!accounts_.saveSession(session.persistId, session.accountId,
                               created, lastActive, linksJson, errorMsg)) {
        LOG_ERROR("Failed to save session %s: %s",
                  session.persistId.c_str(), errorMsg.c_str());
        return;
    }

    int n = session.scrollback.flushToDb(
        accounts_.db(), session.persistId,
        session.accountId, session.scrollbackKey);
    if (n < 0) {
        LOG_ERROR("Scroll-back flush failed for session %s",
                  session.persistId.c_str());
    }
}

void SessionManager::resumeSavedSession(FrontDoorState& fd,
                                        uint32_t accountId,
                                        const std::vector<uint8_t>& sbKey) {
    AccountManager::SavedSession saved;
    if (!accounts_.loadSession(accountId, saved)) return;

    HydraSession session;
    session.id = nextSessionId_++;
    session.accountId = accountId;
    session.username = fd.pendingUsername;
    session.created = time(nullptr);
    session.lastActivity = session.created;
    session.scrollbackKey = sbKey;
    session.persistId = saved.id;
    session.frontDoors.push_back(fd.handle);

    int loaded = session.scrollback.loadFromDb(
        accounts_.db(), saved.id, accountId, sbKey);

    fd.sessionId = session.id;
    sessions_[session.id] = std::move(session);
    HydraSession& sess = sessions_[fd.sessionId];

    LOG_INFO("Session %lu (%s) restored from SQLite for '%s' (%d lines)",
             (unsigned long)fd.sessionId, saved.id.c_str(),
             fd.pendingUsername.c_str(), loaded > 0 ? loaded : 0);

    sendToClient(fd.handle, "\r\nRestoring saved session...\r\n");

    if (sess.scrollback.count() > 0) {
        sendToClient(fd.handle,
            "-- Scroll-back ("
            + std::to_string(sess.scrollback.count())
            + " lines) --\r\n");

        ReplayContext rctx{fd.handle, fd.encoding,
                           fd.colorDepth, &bridge_};
        sess.scrollback.replay(
            sess.scrollback.count(),
            [](const std::string& text,
               const std::string& source,
               time_t timestamp, void* ctx) {
                auto* rc = static_cast<ReplayContext*>(ctx);
                std::string rendered = rc->bridge->renderForClient(
                    rc->encoding, rc->colorDepth, text);
                rendered += "\r\n";
                int fd = static_cast<int>(rc->handle);
                send(fd, rendered.data(), rendered.size(),
                     MSG_NOSIGNAL);
            },
            &rctx);

        sendToClient(fd.handle, "-- End scroll-back --\r\n");
    }

    showGameMenu(sess, fd.handle);
}

void SessionManager::shutdownSessions() {
    LOG_INFO("Flushing all sessions before shutdown");
    for (auto& [sid, session] : sessions_) {
        flushSession(session);
    }
}
