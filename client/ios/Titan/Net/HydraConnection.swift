import Foundation

#if canImport(GRPC)
import GRPC
import NIOCore
import NIOPosix
import SwiftProtobuf

/// A connection to a game server via Hydra's gRPC GameSession bidi stream.
/// Presents the same callback interface as MudConnection so ContentView can
/// use either transport interchangeably.
@MainActor
class HydraConnection: ObservableObject {
    let name: String
    let host: String
    let port: Int
    let useTls: Bool
    private let username: String
    private let password: String
    private let gameName: String
    private let termWidth: Int
    private let termHeight: Int

    @Published var connected = false
    private var intentionalDisconnect = false
    private var sessionId = ""
    private var eventLoopGroup: EventLoopGroup?
    private var channel: GRPCChannel?
    private var stub: Hydra_HydraServiceAsyncClient?
    private var streamTask: Task<Void, Never>?
    private var inputContinuation: AsyncStream<Hydra_ClientMessage>.Continuation?
    private var outputBuffer = ""
    private(set) var lastActivityAt = Date()

    private static let maxReconnectAttempts = 5
    private static let reconnectDelay: UInt64 = 3_000_000_000 // 3 seconds

    var scrollback: [String] = []
    private let maxScrollback = 20000

    var onLine: ((String) -> Void)?
    var onConnect: (() -> Void)?
    var onDisconnect: (() -> Void)?

    init(name: String, host: String, port: Int,
         username: String, password: String, gameName: String,
         useTls: Bool = true,
         termWidth: Int = 80,
         termHeight: Int = 24) {
        self.name = name
        self.host = host
        self.port = port
        self.username = username
        self.password = password
        self.gameName = gameName
        self.useTls = useTls
        self.termWidth = termWidth
        self.termHeight = termHeight
    }

    var idleSeconds: Int {
        max(0, Int(Date().timeIntervalSince(lastActivityAt)))
    }

    func connect() {
        intentionalDisconnect = false

        streamTask = Task {
            do {
                // Create gRPC channel (reuse or create EventLoopGroup)
                let group: EventLoopGroup
                if let existing = self.eventLoopGroup {
                    group = existing
                } else {
                    group = PlatformSupport.makeEventLoopGroup(loopCount: 1)
                    self.eventLoopGroup = group
                }
                let builder = useTls
                    ? ClientConnection.usingTLSBackedByNIOSSL(on: group)
                    : ClientConnection.insecure(group: group)
                let conn = builder.connect(host: host, port: port)
                channel = conn
                stub = Hydra_HydraServiceAsyncClient(channel: conn)

                guard let stub = stub else { return }
                outputBuffer.removeAll(keepingCapacity: true)

                // Authenticate
                var authReq = Hydra_AuthRequest()
                authReq.username = username
                authReq.password = password
                let authResp = try await stub.authenticate(authReq)

                guard authResp.success else {
                    let err = authResp.error.isEmpty ? "Authentication failed" : authResp.error
                    pushLine("[Hydra] \(err)")
                    return
                }
                sessionId = authResp.sessionID

                // Connect to game
                if !gameName.isEmpty {
                    var connReq = Hydra_ConnectRequest()
                    connReq.sessionID = sessionId
                    connReq.gameName = gameName
                    let options = CallOptions(customMetadata: ["authorization": sessionId])
                    let connResp = try await stub.connect(connReq, callOptions: options)
                    if connResp.success {
                        pushLine("[Hydra] Connected to \(gameName) (link \(connResp.linkNumber))")
                    } else {
                        pushLine("[Hydra] Game connect failed: \(connResp.error)")
                    }
                }

                connected = true
                markActivity()
                pushLine("[Hydra] Session established (\(String(sessionId.prefix(8)))...)")
                onConnect?()

                // Open bidi GameSession stream
                await runGameSession(stub: stub)

                // Stream ended — try reconnect
                if !intentionalDisconnect {
                    await attemptReconnect(stub: stub)
                }

            } catch {
                pushLine("[Hydra] Error: \(error.localizedDescription)")
            }

            connected = false
            try? await channel?.close().get()
            channel = nil
            if !intentionalDisconnect {
                onDisconnect?()
            }
        }
    }

    func disconnect() {
        intentionalDisconnect = true
        connected = false
        inputContinuation?.finish()
        streamTask?.cancel()
        try? channel?.close().wait()
        channel = nil
        outputBuffer.removeAll(keepingCapacity: true)
        try? eventLoopGroup?.syncShutdownGracefully()
        eventLoopGroup = nil
    }

    func sendLine(_ text: String) {
        guard connected else { return }
        var msg = Hydra_ClientMessage()
        msg.inputLine = text
        inputContinuation?.yield(msg)
        markActivity()
    }

    // MARK: - GameSession Stream

    private func runGameSession(stub: Hydra_HydraServiceAsyncClient) async {
        let options = CallOptions(customMetadata: ["authorization": sessionId])

        let (inputStream, continuation) = AsyncStream<Hydra_ClientMessage>.makeStream()
        inputContinuation = continuation

        // Send initial preferences as first message on the stream.
        var prefs = Hydra_SetPreferences()
        prefs.colorFormat = .ansiTruecolor
        prefs.terminalWidth = UInt32(termWidth)
        prefs.terminalHeight = UInt32(termHeight)
        prefs.terminalType = "Titan-iOS"
        var prefsMsg = Hydra_ClientMessage()
        prefsMsg.preferences = prefs
        continuation.yield(prefsMsg)

        // Periodic pings
        let pingTask = Task {
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 60_000_000_000)
                var msg = Hydra_ClientMessage()
                var ping = Hydra_PingMessage()
                ping.clientTimestamp = Int64(Date().timeIntervalSince1970 * 1000)
                msg.ping = ping
                continuation.yield(msg)
            }
        }

        do {
            let stream = stub.gameSession(inputStream, callOptions: options)
            for try await msg in stream {
                dispatchServerMessage(msg)
            }
        } catch {
            if !intentionalDisconnect {
                pushLine("[Hydra] Stream error: \(error.localizedDescription)")
            }
        }

        pingTask.cancel()
        connected = false
    }

    private func attemptReconnect(stub: Hydra_HydraServiceAsyncClient) async {
        guard !intentionalDisconnect, !sessionId.isEmpty else { return }

        for attempt in 1...Self.maxReconnectAttempts {
            pushLine("[Hydra] Stream lost, reconnecting (attempt \(attempt))...")
            try? await Task.sleep(nanoseconds: Self.reconnectDelay)

            guard !intentionalDisconnect else { return }

            connected = true
            outputBuffer.removeAll(keepingCapacity: true)
            markActivity()
            await runGameSession(stub: stub)

            if intentionalDisconnect { return }
            connected = false
        }

        pushLine("[Hydra] Reconnect failed after \(Self.maxReconnectAttempts) attempts")
    }

    private func dispatchServerMessage(_ msg: Hydra_ServerMessage) {
        switch msg.payload {
        case .gameOutput(let output):
            dispatchGameOutput(output)
        case .gmcp(let gmcp):
            pushLine("[GMCP \(gmcp.package)] \(gmcp.json)")
        case .notice(let notice):
            pushLine("[Hydra] \(notice.text)")
        case .linkEvent(let ev):
            pushLine("[Hydra] Link \(ev.linkNumber) (\(ev.gameName)): \(ev.newState)")
        case .pong:
            break
        case .none:
            break
        }
    }

    private func dispatchGameOutput(_ output: Hydra_GameOutput) {
        outputBuffer += output.text

        while let newline = outputBuffer.firstIndex(of: "\n") {
            var line = String(outputBuffer[..<newline])
            outputBuffer.removeSubrange(...newline)
            if line.hasSuffix("\r") {
                line.removeLast()
            }
            pushLine(line)
        }

        if output.endOfRecord, !outputBuffer.isEmpty {
            var line = outputBuffer
            outputBuffer.removeAll(keepingCapacity: true)
            if line.hasSuffix("\r") {
                line.removeLast()
            }
            pushLine(line)
        }
    }

    // MARK: - Hydra Session RPCs

    func rpcConnectGame(_ gameName: String) async -> String {
        guard let stub = stub else { return "[Hydra] Not connected." }
        do {
            var req = Hydra_ConnectRequest()
            req.sessionID = sessionId
            req.gameName = gameName
            let options = CallOptions(customMetadata: ["authorization": sessionId])
            let resp = try await stub.connect(req, callOptions: options)
            return resp.success
                ? "[Hydra] Connected to \(gameName) (link \(resp.linkNumber))"
                : "[Hydra] Connect failed: \(resp.error)"
        } catch { return "[Hydra] Error: \(error.localizedDescription)" }
    }

    func rpcSwitchLink(_ linkNumber: Int32) async -> String {
        guard let stub = stub else { return "[Hydra] Not connected." }
        do {
            var req = Hydra_SwitchRequest()
            req.sessionID = sessionId
            req.linkNumber = linkNumber
            let options = CallOptions(customMetadata: ["authorization": sessionId])
            let resp = try await stub.switchLink(req, callOptions: options)
            return resp.success
                ? "[Hydra] Switched to link \(linkNumber)"
                : "[Hydra] Switch failed: \(resp.error)"
        } catch { return "[Hydra] Error: \(error.localizedDescription)" }
    }

    func rpcListLinks() async -> [String] {
        guard let stub = stub else { return ["[Hydra] Not connected."] }
        do {
            var req = Hydra_SessionRequest()
            req.sessionID = sessionId
            let options = CallOptions(customMetadata: ["authorization": sessionId])
            let resp = try await stub.listLinks(req, callOptions: options)
            if resp.links.isEmpty { return ["[Hydra] No active links."] }
            var lines = ["[Hydra] Active links:"]
            for li in resp.links {
                var line = "  Link \(li.number): \(li.gameName) (\(li.state))"
                if li.active { line += " [active]" }
                if !li.character.isEmpty { line += " as \(li.character)" }
                lines.append(line)
            }
            return lines
        } catch { return ["[Hydra] Error: \(error.localizedDescription)"] }
    }

    func rpcDisconnectLink(_ linkNumber: Int32) async -> String {
        guard let stub = stub else { return "[Hydra] Not connected." }
        do {
            var req = Hydra_DisconnectRequest()
            req.sessionID = sessionId
            req.linkNumber = linkNumber
            let options = CallOptions(customMetadata: ["authorization": sessionId])
            let resp = try await stub.disconnectLink(req, callOptions: options)
            return resp.success
                ? "[Hydra] Disconnected link \(linkNumber)"
                : "[Hydra] Disconnect failed: \(resp.error)"
        } catch { return "[Hydra] Error: \(error.localizedDescription)" }
    }

    func rpcGetSession() async -> [String] {
        guard let stub = stub else { return ["[Hydra] Not connected."] }
        do {
            var req = Hydra_SessionRequest()
            req.sessionID = sessionId
            let options = CallOptions(customMetadata: ["authorization": sessionId])
            let resp = try await stub.getSession(req, callOptions: options)
            return [
                "[Hydra] Session \(String(resp.sessionID.prefix(8)))...",
                "  User: \(resp.username)",
                "  State: \(resp.state)",
                "  Active link: \(resp.activeLink)",
                "  Links: \(resp.links.count)",
                "  Scrollback: \(resp.scrollbackLines) lines",
            ]
        } catch { return ["[Hydra] Error: \(error.localizedDescription)"] }
    }

    func rpcDetachSession() async -> String {
        guard let stub = stub else { return "[Hydra] Not connected." }
        do {
            var req = Hydra_SessionRequest()
            req.sessionID = sessionId
            let options = CallOptions(customMetadata: ["authorization": sessionId])
            _ = try await stub.detachSession(req, callOptions: options)
            connected = false
            return "[Hydra] Session detached. Reconnect to resume."
        } catch { return "[Hydra] Error: \(error.localizedDescription)" }
    }

    // MARK: - Helpers

    private func pushLine(_ text: String) {
        markActivity()
        scrollback.append(text)
        while scrollback.count > maxScrollback { scrollback.removeFirst() }
        onLine?(text)
    }

    private func markActivity() {
        lastActivityAt = Date()
    }
}

#endif // canImport(GRPC)
