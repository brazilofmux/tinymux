#include "session_manager.h"
#include "crypto.h"
#include "hydra_log.h"
#include "telnet_utils.h"
#include <color_ops.h>
#include <nlohmann/json.hpp>
#ifdef GRPC_ENABLED
#include "grpc_web.h"
#include "hydra.pb.h"
#endif
#include <chrono>
#include <cstring>
#include <algorithm>

// Context for scroll-back replay callback — renders PUA through the bridge.
struct ReplayContext {
    ganl::ConnectionHandle handle;
    ganl::EncodingType encoding;
    ColorDepth colorDepth;
    TelnetBridge* bridge;
    ganl::NetworkEngine* engine;  // for safeWrite via postWrite
};

// Telnet constants for GMCP parsing (from shared telnet_utils.h)
static constexpr unsigned char T_IAC  = telnet::IAC;
static constexpr unsigned char T_SB   = telnet::SB;
static constexpr unsigned char T_SE   = telnet::SE;
static constexpr unsigned char T_GMCP = telnet::GMCP;
static constexpr unsigned char T_WILL = telnet::WILL;
static constexpr unsigned char T_DO   = telnet::DO;

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

// buildGmcpFrame() is now in telnet_utils.h (shared with grpc_server.cpp)

// Send Hydra.Links GMCP to all GMCP-enabled front-doors of a session.
// Called whenever link state changes (connect, disconnect, reconnect).
static void sendHydraLinksGmcp(HydraSession& session,
                               std::map<ganl::ConnectionHandle, FrontDoorState>& frontDoors,
                               std::function<void(ganl::ConnectionHandle, const std::string&)> writeFn) {
    nlohmann::json linksArr = nlohmann::json::array();
    for (size_t i = 0; i < session.links.size(); i++) {
        const auto& l = session.links[i];
        const char* state = "unknown";
        switch (l.state) {
        case LinkState::Connecting:     state = "connecting"; break;
        case LinkState::TlsHandshaking: state = "tls"; break;
        case LinkState::Negotiating:    state = "negotiating"; break;
        case LinkState::AutoLoggingIn:  state = "logging-in"; break;
        case LinkState::Active:         state = "active"; break;
        case LinkState::Reconnecting:   state = "reconnecting"; break;
        case LinkState::Suspended:      state = "suspended"; break;
        case LinkState::Dead:           state = "dead"; break;
        }
        linksArr.push_back({
            {"number", static_cast<int>(i) + 1},
            {"game", l.gameName},
            {"state", state},
            {"active", i == session.activeLink},
        });
    }
    std::string gmcpPayload = "Hydra.Links " + linksArr.dump();
    std::string frame = buildGmcpFrame(gmcpPayload);

    for (auto h : session.frontDoors) {
        auto fdIt = frontDoors.find(h);
        if (fdIt != frontDoors.end() && fdIt->second.gmcpEnabled) {
            writeFn(h, frame);
        }
    }
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

std::atomic<size_t> SessionManager::globalScrollbackBytes_{0};

SessionManager::SessionManager(ganl::NetworkEngine& engine,
                               AccountManager& accounts,
                               const HydraConfig& config)
    : engine_(engine), accounts_(accounts), config_(config) {
}

SessionManager::~SessionManager() {
}

void SessionManager::sendToClient(ganl::ConnectionHandle handle,
                                  const std::string& text) {
    int sockfd = static_cast<int>(handle);

    auto it = frontDoors_.find(handle);
    if (it != frontDoors_.end()) {
        if (it->second.proto == FrontDoorProto::WebSocket) {
            std::string frame = wsEncodeFrame(text);
            safeWrite(handle, frame);
            return;
        }
#ifdef GRPC_ENABLED
        if (it->second.grpcWebSubscribed) {
            // Send as a grpc-web data frame with the text as GameOutput
            hydra::GameOutput go;
            go.set_text(text);
            go.set_source("hydra");
            go.set_timestamp(static_cast<int64_t>(time(nullptr)));

            std::string gwFrame = grpcWebEncodeDataFrame(
                go.SerializeAsString());
            if (it->second.grpcWebTextMode) gwFrame = base64Encode(gwFrame);

            std::string chunk = std::to_string(gwFrame.size())
                + "\r\n" + gwFrame + "\r\n";
            safeWrite(handle, chunk);
            return;
        }
#endif
    }

    safeWrite(handle, text);
}

void SessionManager::safeWrite(ganl::ConnectionHandle handle,
                               const std::string& data) {
    safeWrite(handle, data.data(), data.size());
}

void SessionManager::safeWrite(ganl::ConnectionHandle handle,
                               const char* data, size_t len) {
    ganl::ErrorCode err = 0;
    if (!engine_.postWrite(handle, data, len, err)) {
        char errbuf[128] = {};
#if defined(_GNU_SOURCE)
        // GNU strerror_r returns char* (may not use errbuf)
        const char* errstr = strerror_r(err, errbuf, sizeof(errbuf));
#else
        // XSI strerror_r returns int, writes to errbuf
        strerror_r(err, errbuf, sizeof(errbuf));
        const char* errstr = errbuf;
#endif
        LOG_DEBUG("safeWrite failed for fd %lu: %s",
                  (unsigned long)handle, errstr ? errstr : "unknown");
    }
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

    // Replay cached GMCP state to this front-door (if GMCP-enabled)
    auto fdIt = frontDoors_.find(fdHandle);
    if (fdIt != frontDoors_.end() && fdIt->second.gmcpEnabled) {
        for (const auto& [pkg, json] : session.gmcpCache) {
            safeWrite(fdHandle, buildGmcpFrame(pkg + " " + json));
        }
    }
}

void SessionManager::replayGmcpCache(HydraSession& session,
                                      std::shared_ptr<HydraSession::SubscriberQueue> sq) {
    // Replay cached GMCP state into a gRPC subscriber queue
    for (const auto& [pkg, json] : session.gmcpCache) {
        HydraSession::GmcpItem item;
        item.package = pkg;
        item.json = json;
        item.linkNumber = session.activeLink < session.links.size()
            ? static_cast<int>(session.activeLink) + 1 : 0;
        sq->gmcp.push(std::move(item));
    }
}

// ---- Reverse map helper ----

bool SessionManager::findByBackDoor(ganl::ConnectionHandle bdHandle,
                                    HydraSession*& session,
                                    BackDoorLink*& link,
                                    size_t& linkIdx) {
    auto it = backDoorMap_.find(bdHandle);
    if (it == backDoorMap_.end()) return false;

    auto sit = sessions_.find(it->second.internalSessionId);
    if (sit == sessions_.end()) return false;

    session = &sit->second;
    linkIdx = it->second.linkIndex;
    if (linkIdx >= session->links.size()) return false;
    link = &session->links[linkIdx];
    return true;
}

// ---- Front-door lifecycle ----

void SessionManager::onAccept(ganl::ConnectionHandle handle,
                              const std::string& clientIp) {
    if (!checkConnectionLimits(clientIp)) {
        engine_.closeConnection(handle);
        return;
    }
    recordConnect(clientIp);

    FrontDoorState fd;
    fd.handle = handle;
    fd.loginPhase = FrontDoorState::AwaitUsername;
    fd.proto = FrontDoorProto::Telnet;
    fd.clientIp = clientIp;
    frontDoors_[handle] = fd;

    LOG_INFO("Session manager: new front-door %lu (telnet) from %s",
             (unsigned long)handle, clientIp.c_str());
    showBanner(handle);
}

void SessionManager::onAcceptWebSocket(ganl::ConnectionHandle handle,
                                       const std::string& clientIp) {
    if (!checkConnectionLimits(clientIp)) {
        engine_.closeConnection(handle);
        return;
    }
    recordConnect(clientIp);

    FrontDoorState fd;
    fd.handle = handle;
    fd.loginPhase = FrontDoorState::AwaitUsername;
    fd.proto = FrontDoorProto::WebSocket;
    fd.encoding = ganl::EncodingType::Utf8;
    fd.colorDepth = ColorDepth::Ansi256;
    fd.clientIp = clientIp;
    frontDoors_[handle] = fd;

    LOG_INFO("Session manager: new front-door %lu (websocket) from %s",
             (unsigned long)handle, clientIp.c_str());
}

void SessionManager::onAcceptGrpcWeb(ganl::ConnectionHandle handle,
                                     const std::string& clientIp) {
    if (!checkConnectionLimits(clientIp)) {
        engine_.closeConnection(handle);
        return;
    }
    recordConnect(clientIp);

    FrontDoorState fd;
    fd.handle = handle;
    fd.loginPhase = FrontDoorState::AwaitUsername;
    fd.proto = FrontDoorProto::GrpcWeb;
    fd.encoding = ganl::EncodingType::Utf8;
    fd.colorDepth = ColorDepth::Ansi256;
    fd.clientIp = clientIp;
    frontDoors_[handle] = fd;

    LOG_INFO("Session manager: new front-door %lu (grpc-web) from %s",
             (unsigned long)handle, clientIp.c_str());
}

void SessionManager::onFrontDoorData(ganl::ConnectionHandle handle,
                                     const char* data, size_t len) {
    auto it = frontDoors_.find(handle);
    if (it == frontDoors_.end()) return;

    FrontDoorState& fd = it->second;

    // ---- WebSocket path (also handles WsGameSession during handshake) ----
    if (fd.proto == FrontDoorProto::WebSocket) {
        if (!fd.wsState.handshakeComplete) {
            // Still in HTTP upgrade handshake
            std::string response = wsProcessHandshake(fd.wsState, data, len);
            if (!response.empty()) {
                int sockfd = static_cast<int>(handle);
                safeWrite(handle, response);

                if (!fd.wsState.handshakeOk) {
                    // Handshake failed — close
                    engine_.closeConnection(handle);
                    return;
                }

                // Check if this is a GameSession WebSocket (hydra-gamesession subprotocol)
#ifdef GRPC_ENABLED
                if (fd.wsState.isGameSession) {
                    fd.proto = FrontDoorProto::WsGameSession;
                    LOG_INFO("WebSocket GameSession handshake complete for fd %lu",
                             (unsigned long)handle);
                    // No banner, no session.frontDoors — wait for first-message auth.
                    // Process any trailing data as WsGameSession frames.
                    if (!fd.wsState.handshakeBuf.empty()) {
                        std::string trailing = fd.wsState.handshakeBuf;
                        fd.wsState.handshakeBuf.clear();
                        handleWsGameSessionData(fd, trailing.data(), trailing.size());
                    }
                    return;
                }
#endif

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
                        safeWrite(handle, responses);
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
            safeWrite(handle, responses);
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

    // ---- WebSocket GameSession path ----
#ifdef GRPC_ENABLED
    if (fd.proto == FrontDoorProto::WsGameSession) {
        handleWsGameSessionData(fd, data, len);
        return;
    }
#endif

    // ---- gRPC-Web path ----
#ifdef GRPC_ENABLED
    if (fd.proto == FrontDoorProto::GrpcWeb) {
        if (fd.grpcWebSubscribed) {
            // This fd is a Subscribe stream — ignore incoming data.
            // The client shouldn't be sending anything.
            return;
        }
        fd.httpBuf.append(data, len);
        if (fd.httpBuf.size() > 1024 * 1024) {  // 1MB limit
            LOG_WARN("Front-door %lu: HTTP buffer too large (%zu bytes), closing",
                     (unsigned long)fd.handle, fd.httpBuf.size());
            std::string resp = "HTTP/1.1 413 Payload Too Large\r\n"
                               "Content-Length: 0\r\n\r\n";
            safeWrite(fd.handle, resp);
            engine_.closeConnection(fd.handle);
            return;
        }
        handleGrpcWebRequest(fd);
        return;
    }
#endif

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
    if (!gmcpMsgs.empty() && fd.internalSessionId != InvalidHydraSessionId) {
        auto sit = sessions_.find(fd.internalSessionId);
        if (sit != sessions_.end()) {
            BackDoorLink* active = sit->second.getActiveLink();
            if (active && active->handle != ganl::InvalidConnectionHandle &&
                active->gmcpEnabled) {
                for (const auto& gm : gmcpMsgs) {
                    std::string frame = buildGmcpFrame(gm.payload);
                    safeWrite(active->handle, frame);
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
        } else if (fd.lineBuf.size() < FrontDoorState::MAX_LINE_LENGTH) {
            fd.lineBuf += ch;
        } else {
            // Line too long — drop the connection
            LOG_WARN("Front-door %lu: line too long (%zu bytes), closing",
                     (unsigned long)fd.handle, fd.lineBuf.size());
            engine_.closeConnection(fd.handle);
            return;
        }
    }
}

void SessionManager::onFrontDoorClose(ganl::ConnectionHandle handle) {
    auto it = frontDoors_.find(handle);
    if (it == frontDoors_.end()) return;

    FrontDoorState& fd = it->second;
    HydraSessionId sid = fd.internalSessionId;
    std::string ip = fd.clientIp;

#ifdef GRPC_ENABLED
    // WsGameSession: unsubscribe from output queue (not in session.frontDoors)
    if (fd.proto == FrontDoorProto::WsGameSession && fd.wsGameSessionOQ) {
        std::lock_guard<std::mutex> lock(fd.wsGameSessionOQ->mutex);
        fd.wsGameSessionOQ->removeSubscriber(fd.wsGameSessionSubId);
        LOG_INFO("WsGameSession fd %lu: unsubscribed (sub %d)",
                 (unsigned long)handle, fd.wsGameSessionSubId);
    }
#endif

    frontDoors_.erase(it);
    recordDisconnect(ip);

    if (sid != InvalidHydraSessionId) {
        auto sit = sessions_.find(sid);
        if (sit != sessions_.end()) {
            HydraSession& session = sit->second;
            auto& fds = session.frontDoors;
            fds.erase(std::remove(fds.begin(), fds.end(), handle),
                      fds.end());

            if (fds.empty() && !session.outputQueue->hasSubscribers()) {
                LOG_INFO("Session %lu: no front-doors or subscribers, detaching",
                         (unsigned long)sid);
                session.state = SessionState::Detached;
                flushSession(session);
            }
        }
    }

    LOG_INFO("Front-door %lu closed", (unsigned long)handle);
}

#ifdef GRPC_ENABLED
void SessionManager::handleWsGameSessionData(FrontDoorState& fd,
                                              const char* data, size_t len) {
    // Decode WebSocket frames
    std::string responses;
    auto msgs = wsDecodeFrames(fd.wsState, data, len, responses);
    if (!responses.empty()) {
        safeWrite(fd.handle, responses);
    }

    for (const auto& msg : msgs) {
        if (msg.opcode == WS_OP_CLOSE) {
            engine_.closeConnection(fd.handle);
            return;
        }
        if (msg.opcode != WS_OP_BINARY) continue;

        hydra::ClientMessage cmsg;
        if (!cmsg.ParseFromString(msg.payload)) {
            LOG_WARN("WsGameSession fd %lu: invalid ClientMessage",
                     (unsigned long)fd.handle);
            continue;
        }

        // First-message auth: expect SetPreferences with session_id
        if (!fd.wsGameSessionOQ) {
            if (!cmsg.has_preferences() ||
                cmsg.preferences().session_id().empty()) {
                // Send error and close
                hydra::ServerMessage errMsg;
                auto* notice = errMsg.mutable_system_notice();
                notice->set_text("First message must be SetPreferences with session_id");
                notice->set_severity(hydra::SEVERITY_ERROR);
                std::string frame = wsEncodeFrame(errMsg.SerializeAsString(),
                                                  WS_OP_BINARY);
                safeWrite(fd.handle, frame);
                safeWrite(fd.handle, wsCloseFrame(1008));
                engine_.closeConnection(fd.handle);
                return;
            }

            const auto& prefs = cmsg.preferences();
            std::string sid = prefs.session_id();

            HydraSession* session = findByPersistId(sid);
            if (!session) {
                hydra::ServerMessage errMsg;
                auto* notice = errMsg.mutable_system_notice();
                notice->set_text("Invalid session_id");
                notice->set_severity(hydra::SEVERITY_ERROR);
                std::string frame = wsEncodeFrame(errMsg.SerializeAsString(),
                                                  WS_OP_BINARY);
                safeWrite(fd.handle, frame);
                safeWrite(fd.handle, wsCloseFrame(1008));
                engine_.closeConnection(fd.handle);
                return;
            }

            // Register subscriber
            {
                std::lock_guard<std::mutex> lock(session->outputQueue->mutex);
                auto [id, sq] = session->outputQueue->addSubscriber(true, true);
                fd.wsGameSessionSubId = id;
                fd.wsGameSessionQueue = sq;
            }
            fd.wsGameSessionOQ = session->outputQueue;
            fd.wsGameSessionPersistId = sid;
            fd.internalSessionId = session->internalId;

            session->state = SessionState::Active;
            session->lastActivity = time(nullptr);

            // Replay GMCP cache
            replayGmcpCache(*session, fd.wsGameSessionQueue);

            // Apply preferences (color format, NAWS, ttype)
            if (prefs.color_format() != hydra::COLOR_UNSPECIFIED) {
                std::lock_guard<std::mutex> lock(fd.wsGameSessionOQ->mutex);
                fd.wsGameSessionQueue->renderFormat =
                    static_cast<HydraSession::RenderFormat>(prefs.color_format());
            }
            if (prefs.terminal_width() > 0 || prefs.terminal_height() > 0) {
                BackDoorLink* active = session->getActiveLink();
                if (active && active->handle != ganl::InvalidConnectionHandle) {
                    uint16_t w = prefs.terminal_width() ? static_cast<uint16_t>(prefs.terminal_width()) : 80;
                    uint16_t h = prefs.terminal_height() ? static_cast<uint16_t>(prefs.terminal_height()) : 24;
                    safeWrite(active->handle, buildNawsFrame(w, h));
                }
            }
            if (!prefs.terminal_type().empty()) {
                session->terminalType = prefs.terminal_type();
            }

            LOG_INFO("WsGameSession fd %lu: authenticated session %s (sub %d)",
                     (unsigned long)fd.handle, sid.c_str(), fd.wsGameSessionSubId);
            return;
        }

        // Authenticated — dispatch ClientMessage
        HydraSession* session = findByPersistId(fd.wsGameSessionPersistId);
        if (!session) continue;

        session->lastActivity = time(nullptr);

        if (cmsg.has_input_line()) {
            BackDoorLink* active = session->getActiveLink();
            if (active && active->state == LinkState::Active) {
                std::string line = cmsg.input_line() + "\r\n";
                safeWrite(active->handle, line);
            }
        } else if (cmsg.has_ping()) {
            // Queue pong sentinel for the drain loop
            HydraSession::OutputItem pongItem;
            pongItem.puaText = "\x01PONG\x01" +
                std::to_string(cmsg.ping().client_timestamp());
            pongItem.source = "__pong__";
            pongItem.timestamp = 0;
            pongItem.linkNumber = 0;
            {
                std::lock_guard<std::mutex> lock(fd.wsGameSessionOQ->mutex);
                fd.wsGameSessionQueue->output.push(std::move(pongItem));
            }
            fd.wsGameSessionOQ->cv.notify_all();
        } else if (cmsg.has_preferences()) {
            const auto& prefs = cmsg.preferences();
            if (prefs.color_format() != hydra::COLOR_UNSPECIFIED) {
                std::lock_guard<std::mutex> lock(fd.wsGameSessionOQ->mutex);
                fd.wsGameSessionQueue->renderFormat =
                    static_cast<HydraSession::RenderFormat>(prefs.color_format());
            }
            if (prefs.terminal_width() > 0 || prefs.terminal_height() > 0) {
                BackDoorLink* active = session->getActiveLink();
                if (active && active->handle != ganl::InvalidConnectionHandle) {
                    uint16_t w = prefs.terminal_width() ? static_cast<uint16_t>(prefs.terminal_width()) : 80;
                    uint16_t h = prefs.terminal_height() ? static_cast<uint16_t>(prefs.terminal_height()) : 24;
                    safeWrite(active->handle, buildNawsFrame(w, h));
                }
            }
            if (!prefs.terminal_type().empty()) {
                session->terminalType = prefs.terminal_type();
            }
        } else if (cmsg.has_gmcp()) {
            BackDoorLink* active = session->getActiveLink();
            if (active && active->handle != ganl::InvalidConnectionHandle
                && active->gmcpEnabled) {
                std::string payload = cmsg.gmcp().package() + " " + cmsg.gmcp().json();
                safeWrite(active->handle, buildGmcpFrame(payload));
            }
        }
    }
}

void SessionManager::drainWsGameSessions() {
    for (auto& [handle, fd] : frontDoors_) {
        if (fd.proto != FrontDoorProto::WsGameSession) continue;
        if (!fd.wsGameSessionQueue) continue;

        auto& oq = fd.wsGameSessionOQ;
        auto& sq = fd.wsGameSessionQueue;

        HydraSession::RenderFormat fmt;
        std::vector<HydraSession::OutputItem> outputItems;
        std::vector<HydraSession::GmcpItem> gmcpItems;

        {
            std::lock_guard<std::mutex> lock(oq->mutex);
            if (sq->output.empty() && sq->gmcp.empty()) continue;
            fmt = sq->renderFormat;
            while (!sq->output.empty()) {
                outputItems.push_back(std::move(sq->output.front()));
                sq->output.pop();
            }
            while (!sq->gmcp.empty()) {
                gmcpItems.push_back(std::move(sq->gmcp.front()));
                sq->gmcp.pop();
            }
        }

        for (const auto& item : outputItems) {
            hydra::ServerMessage smsg;
            if (item.source == "__pong__") {
                auto* pong = smsg.mutable_pong();
                int64_t clientTs = 0;
                try { clientTs = std::stoll(item.puaText.substr(6)); }
                catch (...) {}
                pong->set_client_timestamp(clientTs);
                pong->set_server_timestamp(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
            } else {
                auto* go = smsg.mutable_game_output();
                go->set_text(item.render(fmt));
                go->set_source(item.source);
                go->set_timestamp(static_cast<int64_t>(item.timestamp));
                go->set_link_number(item.linkNumber);
            }
            std::string frame = wsEncodeFrame(smsg.SerializeAsString(),
                                              WS_OP_BINARY);
            safeWrite(handle, frame);
        }

        for (const auto& item : gmcpItems) {
            hydra::ServerMessage smsg;
            auto* gm = smsg.mutable_gmcp();
            gm->set_package(item.package);
            gm->set_json(item.json);
            gm->set_link_number(item.linkNumber);
            std::string frame = wsEncodeFrame(smsg.SerializeAsString(),
                                              WS_OP_BINARY);
            safeWrite(handle, frame);
        }
    }
}
#endif

void SessionManager::processLine(FrontDoorState& fd,
                                 const std::string& line) {
    if (fd.loginPhase != FrontDoorState::Authenticated) {
        handleLogin(fd, line);
        return;
    }

    auto sit = sessions_.find(fd.internalSessionId);
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
            // Credential commands require TLS regardless of allow_plaintext
            if (!fd.tlsTransport) {
                sendToClient(fd.handle,
                    "Account creation requires a TLS connection.\r\n"
                    "Username: ");
                return;
            }
            // Rate-limit account creation per IP
            if (!checkAccountCreateRate(fd.clientIp)) {
                sendToClient(fd.handle,
                    "Too many accounts created from this address. "
                    "Try again later.\r\nUsername: ");
                return;
            }
            size_t space = line.find(' ', 7);
            if (space != std::string::npos && space + 1 < line.size()) {
                std::string username = line.substr(7, space - 7);
                std::string password = line.substr(space + 1);
                bool admin = accounts_.isEmpty();

                uint32_t accountId = 0;
                std::string errorMsg;
                if (accounts_.createAccount(username, password, admin,
                                            accountId, errorMsg)) {
                    recordAccountCreate(fd.clientIp);
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
                        session.internalId = nextSessionId_++;
                        session.accountId = authId;
                        session.username = username;
                        session.created = time(nullptr);
                        session.lastActivity = session.created;
                        session.scrollbackKey = sbKey;
                        session.persistId = generatePersistId();
                        session.dbPersistId = session.persistId;
                        session.tokenCreated = session.created;
                        session.frontDoors.push_back(fd.handle);

                        fd.internalSessionId = session.internalId;
                        sessions_[session.internalId] = std::move(session);
                        indexSession(sessions_[fd.internalSessionId]);

                        LOG_INFO("Session %lu (%s) created for '%s'",
                                 (unsigned long)fd.internalSessionId,
                                 sessions_[fd.internalSessionId].persistId.c_str(),
                                 username.c_str());

                        showGameMenu(sessions_[fd.internalSessionId], fd.handle);
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
        if (isLockedOut(fd.clientIp)) {
            sendToClient(fd.handle,
                "\r\nAccount temporarily locked. Try again later.\r\n");
            engine_.closeConnection(fd.handle);
            return;
        }

        std::vector<uint8_t> sbKey;
        uint32_t accountId = accounts_.authenticate(
            fd.pendingUsername, line, sbKey);

        if (accountId == 0) {
            LOG_INFO("Login failed for '%s' from fd %lu",
                     fd.pendingUsername.c_str(),
                     (unsigned long)fd.handle);
            recordLoginFailure(fd.clientIp);
            if (isLockedOut(fd.clientIp)) {
                sendToClient(fd.handle,
                    "\r\nToo many failed attempts. Try again later.\r\n");
                engine_.closeConnection(fd.handle);
                return;
            }
            sendToClient(fd.handle,
                "\r\nLogin failed.\r\n\r\nUsername: ");
            fd.loginPhase = FrontDoorState::AwaitUsername;
            fd.pendingUsername.clear();
            return;
        }

        clearLoginFailures(fd.clientIp);

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
            if (static_cast<int>(existing->frontDoors.size()) >= config_.maxFrontDoorsPerSession) {
                sendToClient(fd.handle,
                    "\r\nMaximum connections to this session reached.\r\n");
                engine_.closeConnection(fd.handle);
                return;
            }
            existing->frontDoors.push_back(fd.handle);
            existing->state = SessionState::Active;
            existing->lastActivity = time(nullptr);
            fd.internalSessionId = existing->internalId;

            // Provide the scrollback key if the session was restored without one
            if (existing->scrollbackKey.empty() && !sbKey.empty()) {
                existing->scrollbackKey = sbKey;
                existing->username = fd.pendingUsername;

                // Load persisted scroll-back now that we have the key
                // Use dbPersistId — persistId may have been rotated by gRPC re-auth
                const std::string& loadId = existing->dbPersistId.empty()
                    ? existing->persistId : existing->dbPersistId;
                existing->scrollback.loadFromDb(
                    accounts_.db(), loadId,
                    existing->accountId, sbKey);
            }

            // Reconnect deferred links (from eager restore without key)
            if (!existing->pendingLinksJson.empty()) {
                std::vector<SavedLinkInfo> savedLinks;
                size_t savedActiveLink = 0;
                parseLinksJson(existing->pendingLinksJson, savedLinks, savedActiveLink);
                existing->activeLink = savedActiveLink;
                restoreSessionLinks(*existing, savedLinks);
                existing->pendingLinksJson.clear();

                sendToClient(fd.handle,
                    "\r\nReconnecting " + std::to_string(savedLinks.size())
                    + " saved link(s)...\r\n");
            }

            LOG_INFO("Session %lu resumed for '%s'",
                     (unsigned long)existing->internalId,
                     fd.pendingUsername.c_str());

            sendToClient(fd.handle, "\r\nResuming session...\r\n");

            if (existing->scrollback.count() > 0) {
                sendToClient(fd.handle,
                    "-- Scroll-back ("
                    + std::to_string(existing->scrollback.count())
                    + " lines) --\r\n");

                ReplayContext rctx{fd.handle, fd.encoding, fd.colorDepth, &bridge_, &engine_};
                existing->scrollback.replay(
                    existing->scrollback.count(),
                    [](const std::string& text,
                       const std::string& source,
                       time_t timestamp, void* ctx) {
                        auto* rc = static_cast<ReplayContext*>(ctx);
                        std::string rendered = rc->bridge->renderForClient(
                            rc->encoding, rc->colorDepth, text);
                        rendered += "\r\n";
                        ganl::ErrorCode werr = 0;
                        rc->engine->postWrite(rc->handle, rendered.data(), rendered.size(), werr);
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
            session.internalId = nextSessionId_++;
            session.accountId = accountId;
            session.username = fd.pendingUsername;
            session.created = time(nullptr);
            session.lastActivity = session.created;
            session.scrollbackKey = sbKey;
            session.persistId = generatePersistId();
            session.dbPersistId = session.persistId;
            session.tokenCreated = session.created;
            session.frontDoors.push_back(fd.handle);

            fd.internalSessionId = session.internalId;
            sessions_[session.internalId] = std::move(session);
            indexSession(sessions_[fd.internalSessionId]);

            LOG_INFO("Session %lu (%s) created for '%s'",
                     (unsigned long)fd.internalSessionId,
                     sessions_[fd.internalSessionId].persistId.c_str(),
                     fd.pendingUsername.c_str());

            sendToClient(fd.handle, "\r\nWelcome, "
                + fd.pendingUsername + ".\r\n");
            showGameMenu(sessions_[fd.internalSessionId], fd.handle);
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
        // Delete persisted session and scroll-back (use dbPersistId for FK)
        const std::string& delId = session.dbPersistId.empty()
            ? session.persistId : session.dbPersistId;
        if (!delId.empty()) {
            accounts_.deleteSession(delId);
        }
        for (auto h : session.frontDoors) {
            sendToClient(h, "Session destroyed. Goodbye.\r\n");
            engine_.closeConnection(h);
            frontDoors_.erase(h);
        }
        unindexSession(session);
        sessions_.erase(session.internalId);
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
            ReplayContext rctx{fdHandle, enc, depth, &bridge_, &engine_};
            session.scrollback.replay(n,
                [](const std::string& text,
                   const std::string& source,
                   time_t timestamp, void* ctx) {
                    auto* rc = static_cast<ReplayContext*>(ctx);
                    std::string rendered = rc->bridge->renderForClient(
                        rc->encoding, rc->colorDepth, text);
                    rendered += "\r\n";
                    ganl::ErrorCode werr = 0;
                        rc->engine->postWrite(rc->handle, rendered.data(), rendered.size(), werr);
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
        // Credential commands require TLS regardless of allow_plaintext
        auto fdIt2 = frontDoors_.find(fdHandle);
        if (fdIt2 != frontDoors_.end() && !fdIt2->second.tlsTransport) {
            sendToClient(fdHandle,
                "Credential storage requires a TLS connection.\r\n");
            return;
        }
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
        if (!accounts_.isAdmin(session.accountId)) {
            sendToClient(fdHandle, "Permission denied (admin required).\r\n");
            return;
        }
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
        if (!accounts_.isAdmin(session.accountId)) {
            sendToClient(fdHandle, "Permission denied (admin required).\r\n");
            return;
        }
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
        if (!accounts_.isAdmin(session.accountId)) {
            sendToClient(fdHandle, "Permission denied (admin required).\r\n");
            return;
        }
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

    // Enforce back-door TLS policy: if tls_required is set but the game
    // doesn't have TLS enabled, reject the connection.
    if (game->tlsRequired && !game->tls) {
        for (auto h : session.frontDoors) {
            sendToClient(h,
                "[" + gameName + ": TLS is required but not configured. "
                "Set 'tls_required no' in the game block to allow "
                "plaintext back-door connections.]\r\n");
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

    backDoorMap_[bdHandle] = {session.internalId, linkIdx};

    for (auto h : session.frontDoors) {
        sendToClient(h,
            "[" + game->name + ": connecting... (link "
            + std::to_string(linkIdx + 1) + ")]\r\n");
    }

    LOG_INFO("Session %lu: connecting link %zu to %s (%s:%u)",
             (unsigned long)session.internalId, linkIdx + 1,
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

    safeWrite(active->handle, converted);
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
             (unsigned long)session->internalId, linkIdx + 1,
             link->gameName.c_str());

    for (auto h : session->frontDoors) {
        sendToClient(h,
            "[" + link->gameName + " (link "
            + std::to_string(linkIdx + 1) + "): connected]\r\n");
    }

    // Send Core.Hello to the game if GMCP is enabled on this link
    if (link->gmcpEnabled) {
        std::string hello = "Core.Hello {\"client\":\"Hydra\",\"version\":\"0.2\"}";
        safeWrite(link->handle, buildGmcpFrame(hello));
    }

    // Send Hydra.Links GMCP to all GMCP-enabled front-doors
    sendHydraLinksGmcp(*session, frontDoors_,
        [this](ganl::ConnectionHandle h, const std::string& data) {
            safeWrite(h, data);
        });

    // Auto-login
    std::string verb, loginName, secret;
    if (accounts_.getLoginSecret(session->accountId, link->gameName,
                                 verb, loginName, secret)) {
        std::string loginCmd = verb + " " + loginName + " " + secret + "\r\n";
        safeWrite(link->handle, loginCmd);

        link->character = loginName;

        LOG_INFO("Session %lu: auto-login sent for link %zu (%s)",
                 (unsigned long)session->internalId, linkIdx + 1,
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
              (unsigned long)session->internalId, linkIdx + 1, strerror(error));

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
        // Cache GMCP state for replay on reconnect
        {
            std::string pkg;
            size_t sp = gm.payload.find(' ');
            if (sp != std::string::npos) {
                pkg = gm.payload.substr(0, sp);
                // Cap cache size — update existing keys freely, but
                // reject new keys once the limit is reached.
                if (session->gmcpCache.count(pkg) ||
                    session->gmcpCache.size() < HydraSession::MAX_GMCP_CACHE_ENTRIES) {
                    session->gmcpCache[pkg] = gm.payload.substr(sp + 1);
                }
            }
        }

        std::string frame = buildGmcpFrame(gm.payload);
        for (auto h : session->frontDoors) {
            auto fdIt = frontDoors_.find(h);
            if (fdIt == frontDoors_.end()) continue;
            if (!fdIt->second.gmcpEnabled) continue;
            safeWrite(h, frame);
        }

        // Push to gRPC GMCP queue — replicated to all subscribers
        {
            std::lock_guard<std::mutex> lock(session->outputQueue->mutex);
            if (session->outputQueue->hasGmcpSubscribers()) {
                // Parse GMCP payload: "Package.Name json_data"
                std::string pkg, json;
                size_t sp = gm.payload.find(' ');
                if (sp != std::string::npos) {
                    pkg = gm.payload.substr(0, sp);
                    json = gm.payload.substr(sp + 1);
                } else {
                    pkg = gm.payload;
                }

                HydraSession::GmcpItem item;
                item.package = pkg;
                item.json = json;
                item.linkNumber = static_cast<int>(linkIdx) + 1;

                session->outputQueue->pushGmcp(std::move(item));
            }
        }
    }

    // Process regular (non-GMCP) data through the color/charset bridge
    if (!regular.empty()) {
        std::string puaText = bridge_.ingestGameOutput(
            link->protoState, regular.data(), regular.size());

        // Track global scrollback memory via atomic counter (O(1) per append).
        size_t oldBytes = session->scrollback.memoryBytes();
        session->scrollback.append(puaText, link->gameName);
        size_t newBytes = session->scrollback.memoryBytes();
        int64_t delta = static_cast<int64_t>(newBytes) - static_cast<int64_t>(oldBytes);
        if (delta > 0) globalScrollbackBytes_.fetch_add(static_cast<size_t>(delta));
        else if (delta < 0) globalScrollbackBytes_.fetch_sub(static_cast<size_t>(-delta));

        if (config_.maxScrollbackMemoryMb > 0) {
            size_t limitBytes = config_.maxScrollbackMemoryMb * 1024 * 1024;
            if (globalScrollbackBytes_.load() >= limitBytes && !scrollbackLimitWarned_) {
                LOG_WARN("Global scrollback memory limit reached (%zu MB)",
                         config_.maxScrollbackMemoryMb);
                scrollbackLimitWarned_ = true;
            } else if (globalScrollbackBytes_.load() < limitBytes) {
                scrollbackLimitWarned_ = false;
            }
        }

        for (auto h : session->frontDoors) {
            auto fdIt = frontDoors_.find(h);
            if (fdIt == frontDoors_.end()) continue;
            const FrontDoorState& fd = fdIt->second;

            std::string rendered = bridge_.renderForClient(
                fd.encoding, fd.colorDepth, puaText);

            if (fd.proto == FrontDoorProto::WebSocket) {
                std::string frame = wsEncodeFrame(rendered);
                safeWrite(h, frame);
#ifdef GRPC_ENABLED
            } else if (fd.grpcWebSubscribed) {
                // Send as chunked grpc-web data frame
                hydra::GameOutput go;
                go.set_text(rendered);
                go.set_source(link->gameName);
                go.set_timestamp(static_cast<int64_t>(time(nullptr)));
                go.set_link_number(static_cast<int>(linkIdx) + 1);

                std::string gwFrame = grpcWebEncodeDataFrame(
                    go.SerializeAsString());
                if (fd.grpcWebTextMode) gwFrame = base64Encode(gwFrame);

                std::string chunk = std::to_string(gwFrame.size())
                    + "\r\n" + gwFrame + "\r\n";
                safeWrite(h, chunk);
#endif
            } else {
                safeWrite(h, rendered);
            }
        }

        // Push PUA-encoded text to gRPC output queue — each subscriber
        // renders at their preferred color format when reading.
        {
            std::lock_guard<std::mutex> lock(session->outputQueue->mutex);
            if (session->outputQueue->hasOutputSubscribers()) {
                HydraSession::OutputItem item;
                item.puaText = puaText;
                item.source = link->gameName;
                item.timestamp = time(nullptr);
                item.linkNumber = static_cast<int>(linkIdx) + 1;

                session->outputQueue->pushOutput(std::move(item));
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
                     (unsigned long)session->internalId, linkIdx + 1,
                     link->gameName.c_str(), delay);

            for (auto h : session->frontDoors) {
                sendToClient(h,
                    "[" + link->gameName + ": disconnected, retrying in "
                    + std::to_string(delay) + "s]\r\n");
            }
            sendHydraLinksGmcp(*session, frontDoors_,
                [this](ganl::ConnectionHandle h, const std::string& d) { safeWrite(h, d); });
            return;
        }
    }

    link->state = LinkState::Dead;

    LOG_INFO("Session %lu: link %zu (%s) lost",
             (unsigned long)session->internalId, linkIdx + 1,
             link->gameName.c_str());

    for (auto h : session->frontDoors) {
        sendToClient(h,
            "[" + link->gameName + " (link "
            + std::to_string(linkIdx + 1) + "): disconnected]\r\n");
    }
    sendHydraLinksGmcp(*session, frontDoors_,
        [this](ganl::ConnectionHandle h, const std::string& d) { safeWrite(h, d); });
}

bool SessionManager::isBackDoor(ganl::ConnectionHandle handle) const {
    return backDoorMap_.find(handle) != backDoorMap_.end();
}

void SessionManager::setFrontDoorTls(ganl::ConnectionHandle handle,
                                      ganl::SecureTransport* transport) {
    auto it = frontDoors_.find(handle);
    if (it != frontDoors_.end()) {
        it->second.tlsTransport = transport;
        it->second.tlsEstablished = false;
    }
}

void SessionManager::runTimers() {
    time_t now = time(nullptr);

    // Periodic scroll-back flush.
    // Sessions with active gRPC subscribers flush every 15 seconds
    // (narrower crash window).  Others flush every 60 seconds.
    for (auto& [sid, session] : sessions_) {
        if (session.scrollback.dirtyCount() == 0) continue;

        bool hasSubscribers = false;
        {
            std::lock_guard<std::mutex> lock(session.outputQueue->mutex);
            hasSubscribers = !session.outputQueue->subscribers.empty();
        }

        int interval = hasSubscribers ? 15 : 60;
        if (now - lastFlush_ >= interval) {
            flushSession(session);
        }
    }
    if (now - lastFlush_ >= 15) {
        lastFlush_ = now;
    }

    // GMCP Core.KeepAlive to active game links (prevents idle disconnects)
    if (now - lastFlush_ < 2) {  // piggyback on the 60s flush interval
        for (auto& [sid, session] : sessions_) {
            for (auto& link : session.links) {
                if (link.state == LinkState::Active && link.gmcpEnabled &&
                    link.handle != ganl::InvalidConnectionHandle) {
                    safeWrite(link.handle,
                        buildGmcpFrame("Core.KeepAlive"));
                }
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

    // Prune stale IP tracker entries every 5 minutes
    if (now - lastIpPrune_ >= 300) {
        lastIpPrune_ = now;
        time_t rateWindow = now - 3600;  // 1-hour window for account creation
        for (auto it = ipTrackers_.begin(); it != ipTrackers_.end(); ) {
            auto& tracker = it->second;

            // Prune expired account-creation timestamps first
            tracker.accountCreateTimes.erase(
                std::remove_if(tracker.accountCreateTimes.begin(),
                               tracker.accountCreateTimes.end(),
                               [rateWindow](time_t t) { return t < rateWindow; }),
                tracker.accountCreateTimes.end());

            // Only erase the entry if truly idle: no connections, no lockout,
            // and no recent account creations within the rate window.
            if (tracker.connectionCount <= 0 &&
                tracker.lockoutUntil <= now &&
                tracker.accountCreateTimes.empty()) {
                it = ipTrackers_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Reap idle and detached sessions
    std::vector<HydraSessionId> toReap;
    for (auto& [sid, session] : sessions_) {
        int timeout = 0;
        if (session.state == SessionState::Detached) {
            timeout = config_.detachedSessionTimeout;
        } else if (session.frontDoors.empty() &&
                   !session.outputQueue->hasSubscribers()) {
            // No front-doors and no gRPC subscribers — effectively detached
            timeout = config_.detachedSessionTimeout;
        } else {
            timeout = config_.sessionIdleTimeout;
        }

        if (timeout > 0 && (now - session.lastActivity) >= timeout) {
            toReap.push_back(sid);
        }
    }
    for (auto sid : toReap) {
        auto sit = sessions_.find(sid);
        if (sit == sessions_.end()) continue;
        HydraSession& session = sit->second;

        LOG_INFO("Session %lu (%s) timed out, destroying",
                 (unsigned long)sid, session.persistId.c_str());

        // Close all links
        for (size_t i = 0; i < session.links.size(); i++) {
            closeLink(session, i);
        }
        // Flush and delete persisted state (use dbPersistId for FK)
        flushSession(session);
        const std::string& reapDelId = session.dbPersistId.empty()
            ? session.persistId : session.dbPersistId;
        if (!reapDelId.empty()) {
            accounts_.deleteSession(reapDelId);
        }
        // Close any remaining front-doors
        for (auto h : session.frontDoors) {
            sendToClient(h, "\r\n[Session timed out. Goodbye.]\r\n");
            engine_.closeConnection(h);
            frontDoors_.erase(h);
        }
        unindexSession(session);
        sessions_.erase(sid);
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
    // Use dbPersistId for SQLite operations — persistId may have been
    // rotated in-memory without updating the database row.
    const std::string& dbId = session.dbPersistId.empty()
        ? session.persistId : session.dbPersistId;
    if (dbId.empty()) return;

    std::string created = std::to_string(session.created);
    std::string lastActive = std::to_string(session.lastActivity);

    // Serialize links via nlohmann/json (handles escaping properly).
    // Dead links are compacted out — remap activeLink to match.
    nlohmann::json linksArr = nlohmann::json::array();
    size_t remappedActive = 0;
    size_t compactIdx = 0;
    for (size_t i = 0; i < session.links.size(); i++) {
        if (session.links[i].state == LinkState::Dead) continue;
        if (i == session.activeLink) remappedActive = compactIdx;
        linksArr.push_back({{"game", session.links[i].gameName},
                            {"character", session.links[i].character}});
        compactIdx++;
    }
    nlohmann::json linksDoc;
    linksDoc["activeLink"] = remappedActive;
    linksDoc["links"] = linksArr;
    std::string linksJson = linksDoc.dump();

    std::string errorMsg;
    if (!accounts_.saveSession(dbId, session.accountId,
                               created, lastActive, linksJson, errorMsg)) {
        LOG_ERROR("Failed to save session %s: %s",
                  dbId.c_str(), errorMsg.c_str());
        return;
    }

    // Flush scroll-back only if we have the encryption key
    if (!session.scrollbackKey.empty()) {
        int n = session.scrollback.flushToDb(
            accounts_.db(), dbId,
            session.accountId, session.scrollbackKey);
        if (n < 0) {
            LOG_ERROR("Scroll-back flush failed for session %s",
                      dbId.c_str());
        }
    }
}

void SessionManager::resumeSavedSession(FrontDoorState& fd,
                                        uint32_t accountId,
                                        const std::vector<uint8_t>& sbKey) {
    AccountManager::SavedSession saved;
    if (!accounts_.loadSession(accountId, saved)) return;

    HydraSession session;
    session.internalId = nextSessionId_++;
    session.accountId = accountId;
    session.username = fd.pendingUsername;
    // Preserve original timestamps from saved data
    session.created = static_cast<time_t>(std::atol(saved.created.c_str()));
    session.lastActivity = static_cast<time_t>(std::atol(saved.lastActive.c_str()));
    if (session.created == 0) session.created = time(nullptr);
    if (session.lastActivity == 0) session.lastActivity = time(nullptr);
    session.scrollbackKey = sbKey;
    session.persistId = saved.persistId;
    session.dbPersistId = session.persistId;
    session.tokenCreated = time(nullptr);
    session.frontDoors.push_back(fd.handle);

    int loaded = session.scrollback.loadFromDb(
        accounts_.db(), saved.persistId, accountId, sbKey);

    // Restore links from JSON
    std::vector<SavedLinkInfo> savedLinks;
    size_t savedActiveLink = 0;
    parseLinksJson(saved.linksJson, savedLinks, savedActiveLink);
    session.activeLink = savedActiveLink;

    fd.internalSessionId = session.internalId;
    HydraSessionId sessId = session.internalId;
    sessions_[sessId] = std::move(session);
    indexSession(sessions_[sessId]);
    HydraSession& sess = sessions_[fd.internalSessionId];

    // Reconnect saved links
    restoreSessionLinks(sess, savedLinks);

    LOG_INFO("Session %lu (%s) restored from SQLite for '%s' (%d lines, %zu links)",
             (unsigned long)fd.internalSessionId, saved.persistId.c_str(),
             fd.pendingUsername.c_str(), loaded > 0 ? loaded : 0,
             savedLinks.size());

    sendToClient(fd.handle, "\r\nRestoring saved session...\r\n");

    if (sess.scrollback.count() > 0) {
        sendToClient(fd.handle,
            "-- Scroll-back ("
            + std::to_string(sess.scrollback.count())
            + " lines) --\r\n");

        ReplayContext rctx{fd.handle, fd.encoding, fd.colorDepth, &bridge_, &engine_};
        sess.scrollback.replay(
            sess.scrollback.count(),
            [](const std::string& text,
               const std::string& source,
               time_t timestamp, void* ctx) {
                auto* rc = static_cast<ReplayContext*>(ctx);
                std::string rendered = rc->bridge->renderForClient(
                    rc->encoding, rc->colorDepth, text);
                rendered += "\r\n";
                ganl::ErrorCode werr = 0;
                        rc->engine->postWrite(rc->handle, rendered.data(), rendered.size(), werr);
            },
            &rctx);

        sendToClient(fd.handle, "-- End scroll-back --\r\n");
    }

    showGameMenu(sess, fd.handle);
}

void SessionManager::shutdownSessions() {
    LOG_INFO("Flushing all sessions before shutdown");
    for (auto& [sid, session] : sessions_) {
        // Notify connected clients
        for (auto h : session.frontDoors) {
            sendToClient(h, "\r\n[Hydra shutting down — reconnect to resume]\r\n");
        }
        // Flush session state and scroll-back to SQLite
        flushSession(session);
        // Wake any blocked gRPC subscribers so they can exit
        session.outputQueue->cv.notify_all();
    }
}

// ---- gRPC support ----

void SessionManager::indexSession(const HydraSession& session) {
    if (!session.persistId.empty()) {
        persistIdIndex_[session.persistId] = session.internalId;
    }
}

void SessionManager::unindexSession(const HydraSession& session) {
    if (!session.persistId.empty()) {
        persistIdIndex_.erase(session.persistId);
    }
}

HydraSession* SessionManager::findByPersistId(const std::string& persistId) {
    auto it = persistIdIndex_.find(persistId);
    if (it == persistIdIndex_.end()) return nullptr;

    auto sit = sessions_.find(it->second);
    if (sit == sessions_.end()) {
        // Stale index entry — clean up
        persistIdIndex_.erase(it);
        return nullptr;
    }

    HydraSession& session = sit->second;

    // Check token TTL
    if (config_.sessionTokenTtl > 0 && session.tokenCreated > 0) {
        time_t now = time(nullptr);
        if ((now - session.tokenCreated) >= config_.sessionTokenTtl) {
            LOG_INFO("Session token expired for '%s' (age %lds)",
                     session.username.c_str(),
                     (long)(now - session.tokenCreated));
            return nullptr;
        }
    }
    return &session;
}

std::string SessionManager::authenticateAndGetSession(
    const std::string& username, const std::string& password) {
    std::vector<uint8_t> sbKey;
    uint32_t accountId = accounts_.authenticate(username, password, sbKey);
    if (accountId == 0) return "";

    // Check for existing in-memory session
    for (auto& [sid, sess] : sessions_) {
        if (sess.accountId == accountId) {
            sess.state = SessionState::Active;
            sess.lastActivity = time(nullptr);

            // Provide scrollback key and reconnect deferred links
            if (sess.scrollbackKey.empty() && !sbKey.empty()) {
                sess.scrollbackKey = sbKey;
                sess.username = username;
                const std::string& loadId = sess.dbPersistId.empty()
                    ? sess.persistId : sess.dbPersistId;
                sess.scrollback.loadFromDb(
                    accounts_.db(), loadId, sess.accountId, sbKey);
            }
            if (!sess.pendingLinksJson.empty()) {
                std::vector<SavedLinkInfo> savedLinks;
                size_t savedActiveLink = 0;
                parseLinksJson(sess.pendingLinksJson, savedLinks, savedActiveLink);
                sess.activeLink = savedActiveLink;
                restoreSessionLinks(sess, savedLinks);
                sess.pendingLinksJson.clear();
                LOG_INFO("Session %s: deferred links reconnected via gRPC auth",
                         sess.persistId.c_str());
            }

            // Rotate session token on re-authentication (in-memory only).
            // The SQLite saved_sessions row keeps the original persistId
            // for crash recovery — the user re-authenticates post-crash
            // and gets a fresh token then.  We do NOT delete+reinsert
            // because ON DELETE CASCADE would destroy scrollback rows.
            unindexSession(sess);
            sess.persistId = generatePersistId();
            sess.tokenCreated = time(nullptr);
            indexSession(sess);

            LOG_INFO("Session token rotated for '%s' (new=%s)",
                     username.c_str(), sess.persistId.c_str());
            return sess.persistId;
        }
    }

    // Check for saved session in SQLite (after restart)
    AccountManager::SavedSession saved;
    if (accounts_.loadSession(accountId, saved)) {
        HydraSession session;
        session.internalId = nextSessionId_++;
        session.accountId = accountId;
        session.username = username;
        session.created = time(nullptr);
        session.lastActivity = session.created;
        session.scrollbackKey = sbKey;
        session.persistId = saved.persistId;
        session.dbPersistId = session.persistId;
        session.tokenCreated = session.created;

        session.scrollback.loadFromDb(
            accounts_.db(), saved.persistId, accountId, sbKey);

        // Restore links from JSON
        std::vector<SavedLinkInfo> savedLinks;
        size_t savedActiveLink = 0;
        parseLinksJson(saved.linksJson, savedLinks, savedActiveLink);
        session.activeLink = savedActiveLink;

        HydraSessionId sessId = session.internalId;
        sessions_[sessId] = std::move(session);
        indexSession(sessions_[sessId]);
        restoreSessionLinks(sessions_[sessId], savedLinks);

        LOG_INFO("Session %lu (%s) restored via gRPC for '%s' (%zu links)",
                 (unsigned long)sessId, saved.persistId.c_str(),
                 username.c_str(), savedLinks.size());
        return saved.persistId;
    }

    // New session
    HydraSession session;
    session.internalId = nextSessionId_++;
    session.accountId = accountId;
    session.username = username;
    session.created = time(nullptr);
    session.lastActivity = session.created;
    session.scrollbackKey = sbKey;
    session.persistId = generatePersistId();
    session.dbPersistId = session.persistId;
    session.tokenCreated = session.created;

    std::string pid = session.persistId;
    sessions_[session.internalId] = std::move(session);
    indexSession(sessions_[nextSessionId_ - 1]);

    LOG_INFO("Session %lu (%s) created via gRPC for '%s'",
             (unsigned long)(nextSessionId_ - 1), pid.c_str(),
             username.c_str());
    return pid;
}

std::string SessionManager::createAccountAndGetSession(
    const std::string& username, const std::string& password,
    const std::string& clientIp, std::string& errorOut) {
    // Rate-limit account creation per IP
    if (!clientIp.empty() && !checkAccountCreateRate(clientIp)) {
        errorOut = "too many accounts created from this address";
        return "";
    }

    bool admin = accounts_.isEmpty();
    uint32_t accountId = 0;
    if (!accounts_.createAccount(username, password, admin,
                                 accountId, errorOut)) {
        return "";
    }

    if (!clientIp.empty()) {
        recordAccountCreate(clientIp);
    }

    // Auto-login
    return authenticateAndGetSession(username, password);
}

// ---- OutputItem rendering ----

std::string HydraSession::OutputItem::render(RenderFormat fmt) const {
    if (puaText.empty()) return puaText;
    if (fmt == RenderFormat::PuaUtf8) return puaText;

    const unsigned char* src =
        reinterpret_cast<const unsigned char*>(puaText.data());
    size_t srcLen = puaText.size();

    // Truecolor SGR expansion can be ~4x; allocate generous heap buffer.
    size_t bufSize = srcLen * 4 + 256;
    std::vector<unsigned char> buf(bufSize);
    size_t n = 0;

    switch (fmt) {
    case RenderFormat::Unspecified:  // fall through to TrueColor default
    case RenderFormat::TrueColor:
        n = co_render_truecolor(buf.data(), src, srcLen, 0);
        break;
    case RenderFormat::Ansi256:
        n = co_render_ansi256(buf.data(), src, srcLen, 0);
        break;
    case RenderFormat::Ansi16:
        n = co_render_ansi16(buf.data(), src, srcLen, 0);
        break;
    case RenderFormat::PuaUtf8:
        return puaText;  // unreachable, handled above
    case RenderFormat::Plain:
        n = co_strip_color(buf.data(), src, srcLen);
        break;
    }

    return std::string(reinterpret_cast<char*>(buf.data()), n);
}

// ---- gRPC-Web request handler ----

#ifdef GRPC_ENABLED
void SessionManager::handleGrpcWebRequest(FrontDoorState& fd) {
    HttpRequest req;
    if (!parseHttpRequest(fd.httpBuf, req)) return;  // incomplete

    int sockfd = static_cast<int>(fd.handle);

    // Extract Origin header for CORS
    std::string requestOrigin;
    auto originIt = req.headers.find("origin");
    if (originIt != req.headers.end()) requestOrigin = originIt->second;

    // CORS preflight
    if (req.method == "OPTIONS") {
        std::string resp = corsPreflightResponse(requestOrigin,
                                                  config_.corsOrigins);
        safeWrite(fd.handle, resp);
        fd.httpBuf.clear();
        return;
    }

    if (req.method != "POST") {
        std::string resp = "HTTP/1.1 405 Method Not Allowed\r\n"
                           "Content-Length: 0\r\n\r\n";
        safeWrite(fd.handle, resp);
        fd.httpBuf.clear();
        return;
    }

    // Decode grpc-web body
    auto ctIt = req.headers.find("content-type");
    bool isText = ctIt != req.headers.end() && isGrpcWebTextContentType(ctIt->second);

    std::string protoBody;
    grpcWebDecodeRequest(req.body, isText, protoBody);

    // Extract auth token from headers
    std::string authToken;
    auto authIt = req.headers.find("authorization");
    if (authIt != req.headers.end()) authToken = authIt->second;

    // Route by path: /hydra.HydraService/MethodName
    std::string method;
    auto slash = req.path.rfind('/');
    if (slash != std::string::npos) {
        method = req.path.substr(slash + 1);
    }

    // Response content type matches request
    std::string respContentType = isText ? "application/grpc-web-text+proto"
                                         : "application/grpc-web+proto";

    // Helper to send a unary grpc-web response
    auto sendUnaryResponse = [&](const std::string& respProto, int status,
                                  const std::string& msg = "") {
        std::string body = grpcWebEncodeUnaryResponse(respProto, status, msg);
        if (isText) body = base64Encode(body);

        std::string http = "HTTP/1.1 200 OK\r\n"
            "Content-Type: " + respContentType + "\r\n"
            + corsHeaders(requestOrigin, config_.corsOrigins)
            + "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "\r\n" + body;
        safeWrite(fd.handle, http);
    };

    // ---- Dispatch RPCs ----

    if (method == "ListGames") {
        hydra::GameList resp;
        for (const auto& game : config_.games) {
            auto* gi = resp.add_games();
            gi->set_name(game.name);
            gi->set_host(game.host);
            gi->set_port(game.port);
            gi->set_type(game.type == GameType::Local
                ? hydra::GAME_LOCAL : hydra::GAME_REMOTE);
            gi->set_autostart(game.autostart);
        }
        sendUnaryResponse(resp.SerializeAsString(), 0);

    } else if (method == "Authenticate") {
        hydra::AuthRequest rpcReq;
        rpcReq.ParseFromString(protoBody);

        std::string pid = authenticateAndGetSession(
            rpcReq.username(), rpcReq.password());

        hydra::AuthResponse resp;
        if (pid.empty()) {
            resp.set_success(false);
            resp.set_error("authentication failed");
        } else {
            resp.set_success(true);
            resp.set_session_id(pid);
        }
        sendUnaryResponse(resp.SerializeAsString(), 0);

    } else if (method == "CreateAccount") {
        hydra::CreateAccountRequest rpcReq;
        rpcReq.ParseFromString(protoBody);

        std::string errorMsg;
        std::string pid = createAccountAndGetSession(
            rpcReq.username(), rpcReq.password(), fd.clientIp, errorMsg);

        hydra::CreateAccountResponse resp;
        if (pid.empty()) {
            resp.set_success(false);
            resp.set_error(errorMsg.empty() ? "failed" : errorMsg);
        } else {
            resp.set_success(true);
            resp.set_session_id(pid);
        }
        sendUnaryResponse(resp.SerializeAsString(), 0);

    } else if (method == "GetSession") {
        hydra::SessionRequest rpcReq;
        rpcReq.ParseFromString(protoBody);
        std::string sid = authToken.empty() ? rpcReq.session_id() : authToken;

        HydraSession* s = findByPersistId(sid);
        if (!s) {
            sendUnaryResponse("", 5, "session not found");  // NOT_FOUND
        } else {
            hydra::SessionInfo resp;
            resp.set_session_id(s->persistId);
            resp.set_username(s->username);
            resp.set_active_link(s->links.empty() ? 0
                : static_cast<int>(s->activeLink) + 1);
            resp.set_scrollback_lines(static_cast<int>(s->scrollback.count()));
            resp.set_state(s->state == SessionState::Active
                ? hydra::SESSION_ACTIVE : hydra::SESSION_DETACHED);
            resp.set_created(static_cast<int64_t>(s->created));
            resp.set_last_activity(static_cast<int64_t>(s->lastActivity));
            sendUnaryResponse(resp.SerializeAsString(), 0);
        }

    } else if (method == "SendInput") {
        hydra::InputRequest rpcReq;
        rpcReq.ParseFromString(protoBody);
        std::string sid = authToken.empty() ? rpcReq.session_id() : authToken;

        HydraSession* s = findByPersistId(sid);
        hydra::InputResponse resp;
        if (!s) {
            resp.set_error("session not found");
        } else {
            BackDoorLink* active = s->getActiveLink();
            if (active && active->state == LinkState::Active) {
                std::string data = rpcReq.line() + "\r\n";
                safeWrite(active->handle, data);
                resp.set_success(true);
            } else {
                resp.set_error("no active link");
            }
        }
        sendUnaryResponse(resp.SerializeAsString(), 0);

    } else if (method == "Connect") {
        hydra::ConnectRequest rpcReq;
        rpcReq.ParseFromString(protoBody);
        std::string sid = authToken.empty() ? rpcReq.session_id() : authToken;

        HydraSession* s = findByPersistId(sid);
        hydra::ConnectResponse resp;
        if (!s) {
            resp.set_error("session not found");
        } else {
            size_t before = s->links.size();
            connectToGame(*s, rpcReq.game_name());
            if (s->links.size() > before) {
                resp.set_success(true);
                resp.set_link_number(static_cast<int>(s->links.size()));
            } else {
                resp.set_error("connect failed");
            }
        }
        sendUnaryResponse(resp.SerializeAsString(), 0);

    } else if (method == "ListLinks") {
        hydra::SessionRequest rpcReq;
        rpcReq.ParseFromString(protoBody);
        std::string sid = authToken.empty() ? rpcReq.session_id() : authToken;

        HydraSession* s = findByPersistId(sid);
        if (!s) {
            sendUnaryResponse("", 5, "session not found");
        } else {
            hydra::LinkList resp;
            for (size_t i = 0; i < s->links.size(); i++) {
                auto* li = resp.add_links();
                li->set_number(static_cast<int>(i) + 1);
                li->set_game_name(s->links[i].gameName);
                li->set_character(s->links[i].character);
                li->set_active(i == s->activeLink);
                li->set_gmcp_enabled(s->links[i].gmcpEnabled);
            }
            sendUnaryResponse(resp.SerializeAsString(), 0);
        }

    } else if (method == "Ping") {
        hydra::SessionRequest rpcReq;
        rpcReq.ParseFromString(protoBody);
        std::string sid = authToken.empty() ? rpcReq.session_id() : authToken;

        HydraSession* s = findByPersistId(sid);
        if (s) s->lastActivity = time(nullptr);
        hydra::Empty resp;
        sendUnaryResponse(resp.SerializeAsString(), s ? 0 : 5);

    } else if (method == "Subscribe") {
        // Server-streaming: register this connection as a live subscriber.
        // It stays open and receives chunked grpc-web data frames as
        // game output arrives in onBackDoorData().
        hydra::SessionRequest rpcReq;
        rpcReq.ParseFromString(protoBody);
        std::string sid = authToken.empty() ? rpcReq.session_id() : authToken;

        HydraSession* s = findByPersistId(sid);
        if (!s) {
            sendUnaryResponse("", 5, "session not found");
        } else {
            // Send HTTP headers for chunked streaming
            std::string httpHdr = "HTTP/1.1 200 OK\r\n"
                "Content-Type: " + respContentType + "\r\n"
                + corsHeaders(requestOrigin, config_.corsOrigins)
                + "Transfer-Encoding: chunked\r\n"
                "\r\n";
            safeWrite(fd.handle, httpHdr);

            // Mark this FrontDoorState as a live grpc-web subscriber.
            // onBackDoorData() will send output to it as chunked frames.
            fd.grpcWebSubscribed = true;
            fd.grpcWebSessionId = s->persistId;
            fd.grpcWebTextMode = isText;
            fd.internalSessionId = s->internalId;

            // Add to session's front-door list so it receives output
            s->frontDoors.push_back(fd.handle);
            s->state = SessionState::Active;

            // Forward terminal size to the active game link if provided
            if (rpcReq.terminal_width() > 0 || rpcReq.terminal_height() > 0) {
                BackDoorLink* active = s->getActiveLink();
                if (active && active->handle != ganl::InvalidConnectionHandle) {
                    uint16_t w = rpcReq.terminal_width() ? static_cast<uint16_t>(rpcReq.terminal_width()) : 80;
                    uint16_t h = rpcReq.terminal_height() ? static_cast<uint16_t>(rpcReq.terminal_height()) : 24;
                    safeWrite(active->handle, buildNawsFrame(w, h));
                }
            }

            LOG_INFO("grpc-web Subscribe: fd %lu subscribed to session %s",
                     (unsigned long)fd.handle, s->persistId.c_str());

            // Don't clear httpBuf — we won't process further HTTP
            // requests on this fd (it's now a streaming connection)
            return;
        }

    } else if (method == "GetScrollBack") {
        hydra::ScrollBackRequest rpcReq;
        rpcReq.ParseFromString(protoBody);
        std::string sid = authToken.empty() ? rpcReq.session_id() : authToken;

        HydraSession* s = findByPersistId(sid);
        if (!s) {
            sendUnaryResponse("", 5, "session not found");
        } else {
            hydra::ScrollBackResponse resp;
            size_t n = rpcReq.max_lines() > 0
                ? static_cast<size_t>(rpcReq.max_lines())
                : s->scrollback.count();
            struct Ctx { hydra::ScrollBackResponse* resp; };
            Ctx ctx{&resp};
            s->scrollback.replay(n,
                [](const std::string& text, const std::string& source,
                   time_t timestamp, void* c) {
                    auto* rc = static_cast<Ctx*>(c);
                    auto* line = rc->resp->add_lines();
                    line->set_text(text);
                    line->set_source(source);
                    line->set_timestamp(static_cast<int64_t>(timestamp));
                },
                &ctx);
            sendUnaryResponse(resp.SerializeAsString(), 0);
        }

    } else if (method == "SwitchLink") {
        hydra::SwitchRequest rpcReq;
        rpcReq.ParseFromString(protoBody);
        std::string sid = authToken.empty() ? rpcReq.session_id() : authToken;

        HydraSession* s = findByPersistId(sid);
        hydra::SwitchResponse resp;
        if (!s) {
            resp.set_error("session not found");
        } else {
            size_t idx = static_cast<size_t>(rpcReq.link_number() - 1);
            if (idx >= s->links.size()) {
                resp.set_error("invalid link number");
            } else {
                s->activeLink = idx;
                resp.set_success(true);
            }
        }
        sendUnaryResponse(resp.SerializeAsString(), 0);

    } else if (method == "DisconnectLink") {
        hydra::DisconnectRequest rpcReq;
        rpcReq.ParseFromString(protoBody);
        std::string sid = authToken.empty() ? rpcReq.session_id() : authToken;

        HydraSession* s = findByPersistId(sid);
        hydra::DisconnectResponse resp;
        if (!s) {
            resp.set_error("session not found");
        } else {
            size_t idx = static_cast<size_t>(rpcReq.link_number() - 1);
            if (idx >= s->links.size()) {
                resp.set_error("invalid link number");
            } else {
                closeLink(*s, idx);
                resp.set_success(true);
            }
        }
        sendUnaryResponse(resp.SerializeAsString(), 0);

    } else {
        // Unimplemented method
        sendUnaryResponse("", 12, "method not found: " + method);
    }

    fd.httpBuf.clear();
}
#else
void SessionManager::handleGrpcWebRequest(FrontDoorState&) {}
void SessionManager::handleWsGameSessionData(FrontDoorState&, const char*, size_t) {}
void SessionManager::drainWsGameSessions() {}
#endif

// ---- Rate limiting ----

std::string SessionManager::getClientIp(ganl::ConnectionHandle) const {
    return "";  // Placeholder — IP is passed via onAccept now
}

bool SessionManager::checkConnectionLimits(const std::string& ip) {
    if (ip.empty()) return true;  // can't enforce without IP

    auto& tracker = ipTrackers_[ip];

    // Check per-IP connection limit
    if (tracker.connectionCount >= config_.maxConnectionsPerIp) {
        LOG_WARN("Rate limit: %s has %d connections (max %d)",
                 ip.c_str(), tracker.connectionCount, config_.maxConnectionsPerIp);
        return false;
    }

    // Check connection rate limit (per minute)
    time_t now = time(nullptr);
    time_t oneMinuteAgo = now - 60;

    // Prune old timestamps
    auto& times = tracker.connectTimes;
    times.erase(
        std::remove_if(times.begin(), times.end(),
            [oneMinuteAgo](time_t t) { return t < oneMinuteAgo; }),
        times.end());

    if (static_cast<int>(times.size()) >= config_.connectRateLimit) {
        LOG_WARN("Rate limit: %s exceeded %d connections/minute",
                 ip.c_str(), config_.connectRateLimit);
        return false;
    }

    return true;
}

void SessionManager::recordConnect(const std::string& ip) {
    if (ip.empty()) return;
    auto& tracker = ipTrackers_[ip];
    tracker.connectionCount++;
    tracker.connectTimes.push_back(time(nullptr));
}

void SessionManager::recordDisconnect(const std::string& ip) {
    if (ip.empty()) return;
    auto it = ipTrackers_.find(ip);
    if (it != ipTrackers_.end() && it->second.connectionCount > 0) {
        it->second.connectionCount--;
    }
}

void SessionManager::recordLoginFailure(const std::string& ip) {
    if (ip.empty()) return;
    auto& tracker = ipTrackers_[ip];
    tracker.failedLogins++;
    if (tracker.failedLogins >= config_.failedLoginLockout) {
        tracker.lockoutUntil = time(nullptr) + config_.failedLoginLockoutMinutes * 60;
        LOG_WARN("Rate limit: %s locked out for %d minutes after %d failed logins",
                 ip.c_str(), config_.failedLoginLockoutMinutes, tracker.failedLogins);
    }
}

void SessionManager::clearLoginFailures(const std::string& ip) {
    if (ip.empty()) return;
    auto it = ipTrackers_.find(ip);
    if (it != ipTrackers_.end()) {
        it->second.failedLogins = 0;
        it->second.lockoutUntil = 0;
    }
}

bool SessionManager::isLockedOut(const std::string& ip) {
    if (ip.empty()) return false;
    auto it = ipTrackers_.find(ip);
    if (it == ipTrackers_.end()) return false;
    if (it->second.lockoutUntil == 0) return false;
    if (time(nullptr) >= it->second.lockoutUntil) {
        // Lockout expired
        it->second.lockoutUntil = 0;
        it->second.failedLogins = 0;
        return false;
    }
    return true;
}

bool SessionManager::checkAccountCreateRate(const std::string& ip) {
    if (ip.empty()) return true;
    auto it = ipTrackers_.find(ip);
    if (it == ipTrackers_.end()) return true;

    // Allow max 2 account creations per hour per IP
    time_t now = time(nullptr);
    time_t window = now - 3600;
    auto& times = it->second.accountCreateTimes;

    // Prune old entries
    times.erase(std::remove_if(times.begin(), times.end(),
        [window](time_t t) { return t < window; }),
        times.end());

    return times.size() < 2;
}

void SessionManager::recordAccountCreate(const std::string& ip) {
    if (ip.empty()) return;
    ipTrackers_[ip].accountCreateTimes.push_back(time(nullptr));
}

// ---- Phase 4: Session serialization ----

bool SessionManager::parseLinksJson(const std::string& jsonStr,
                                    std::vector<SavedLinkInfo>& links,
                                    size_t& activeLink) {
    links.clear();
    activeLink = 0;

    if (jsonStr.empty()) return false;

    try {
        auto doc = nlohmann::json::parse(jsonStr);

        if (doc.contains("activeLink") && doc["activeLink"].is_number()) {
            activeLink = doc["activeLink"].get<size_t>();
        }

        if (doc.contains("links") && doc["links"].is_array()) {
            for (const auto& entry : doc["links"]) {
                SavedLinkInfo li;
                if (entry.contains("game") && entry["game"].is_string()) {
                    li.game = entry["game"].get<std::string>();
                }
                if (entry.contains("character") && entry["character"].is_string()) {
                    li.character = entry["character"].get<std::string>();
                }
                if (!li.game.empty()) {
                    links.push_back(std::move(li));
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_WARN("Failed to parse links_json: %s", e.what());
        return false;
    }

    return !links.empty();
}

void SessionManager::restoreSessionLinks(HydraSession& session,
                                         const std::vector<SavedLinkInfo>& savedLinks) {
    for (const auto& sl : savedLinks) {
        // Resolve game config by name
        const GameConfig* game = nullptr;
        for (const auto& g : config_.games) {
            if (g.name == sl.game) { game = &g; break; }
        }
        if (!game) {
            LOG_WARN("Session %s: saved link for unknown game '%s', skipping",
                     session.persistId.c_str(), sl.game.c_str());
            continue;
        }

        // Track link count to verify connectToGame actually added one
        size_t before = session.links.size();
        connectToGame(session, sl.game);

        if (session.links.size() > before) {
            session.links.back().character = sl.character;
        } else {
            LOG_WARN("Session %s: connectToGame('%s') did not create a link",
                     session.persistId.c_str(), sl.game.c_str());
        }
    }

    // Restore activeLink index (clamp to valid range)
    if (session.activeLink >= session.links.size() && !session.links.empty()) {
        session.activeLink = 0;
    }
}

void SessionManager::restoreAllSessions() {
    auto saved = accounts_.loadAllSessions();
    if (saved.empty()) {
        LOG_INFO("No saved sessions to restore");
        return;
    }

    LOG_INFO("Restoring %zu saved sessions", saved.size());

    for (const auto& s : saved) {
        // Deduplication: skip if this account already has an in-memory session
        bool alreadyLoaded = false;
        for (const auto& [id, sess] : sessions_) {
            if (sess.accountId == s.accountId) {
                alreadyLoaded = true;
                break;
            }
        }
        if (alreadyLoaded) {
            LOG_INFO("Session %s (account %u) already in memory, skipping restore",
                     s.persistId.c_str(), s.accountId);
            continue;
        }

        // Create in-memory session (detached — no front-door yet)
        HydraSession session;
        session.internalId = nextSessionId_++;
        session.accountId = s.accountId;
        // Preserve original timestamps from saved data
        session.created = static_cast<time_t>(std::atol(s.created.c_str()));
        session.lastActivity = static_cast<time_t>(std::atol(s.lastActive.c_str()));
        if (session.created == 0) session.created = time(nullptr);
        if (session.lastActivity == 0) session.lastActivity = session.created;
        session.persistId = s.persistId;
        session.dbPersistId = s.persistId;
        session.state = SessionState::Detached;
        // scrollbackKey is empty — can't decrypt scroll-back until player logs in.
        // Defer link reconnection: back-door links will reconnect when the
        // player authenticates, providing the scrollback key.  This prevents
        // accumulating unflushable output if Hydra crashes again.
        session.pendingLinksJson = s.linksJson;

        HydraSessionId sessId = session.internalId;
        sessions_[sessId] = std::move(session);
        indexSession(sessions_[sessId]);

        LOG_INFO("Restored session %s (account %u, links deferred until login)",
                 s.persistId.c_str(), s.accountId);
    }
}

void SessionManager::dumpStatus() const {
    LOG_INFO("=== Hydra Status Dump ===");
    LOG_INFO("Sessions: %zu  Front-doors: %zu  Back-doors: %zu",
             sessions_.size(), frontDoors_.size(), backDoorMap_.size());
    LOG_INFO("Global scrollback memory: %zu bytes",
             globalScrollbackBytes_.load());

    for (const auto& kv : sessions_) {
        const HydraSession& s = kv.second;
        const char* stateStr = "?";
        switch (s.state) {
            case SessionState::Login:    stateStr = "Login";    break;
            case SessionState::Active:   stateStr = "Active";   break;
            case SessionState::Detached: stateStr = "Detached"; break;
        }

        LOG_INFO("  Session %s [%s] user=%s fd=%zu links=%zu "
                 "scrollback=%zu/%zu dirty=%zu",
                 s.persistId.c_str(), stateStr, s.username.c_str(),
                 s.frontDoors.size(), s.links.size(),
                 s.scrollback.count(), s.scrollback.memoryBytes(),
                 s.scrollback.dirtyCount());

        for (size_t i = 0; i < s.links.size(); i++) {
            const BackDoorLink& link = s.links[i];
            const char* linkStr = "?";
            switch (link.state) {
                case LinkState::Connecting:     linkStr = "Connecting";     break;
                case LinkState::TlsHandshaking: linkStr = "TlsHandshake";  break;
                case LinkState::Negotiating:    linkStr = "Negotiating";    break;
                case LinkState::AutoLoggingIn:  linkStr = "AutoLogin";      break;
                case LinkState::Active:         linkStr = "Active";         break;
                case LinkState::Reconnecting:   linkStr = "Reconnecting";   break;
                case LinkState::Suspended:      linkStr = "Suspended";      break;
                case LinkState::Dead:           linkStr = "Dead";           break;
            }
            LOG_INFO("    Link %zu: game=%s char=%s [%s]%s",
                     i + 1, link.gameName.c_str(), link.character.c_str(),
                     linkStr, (i == s.activeLink) ? " *active*" : "");
        }
    }
    LOG_INFO("=== End Status Dump ===");
}
