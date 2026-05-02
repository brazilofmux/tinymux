import Foundation

#if canImport(GRPCCore)
import GRPCCore
import GRPCNIOTransportHTTP2Posix

/// A connection to a game server via Hydra's gRPC GameSession bidi stream.
/// Presents the same callback interface as MudConnection so ContentView can
/// use either transport interchangeably.
@available(iOS 18.0, macOS 15.0, *)
@MainActor
class HydraConnection: ObservableObject {
    let name: String
    let host: String
    let port: Int
    let useTls: Bool
    private let username: String
    private let password: String
    private let gameName: String
    private(set) var termWidth: Int
    private(set) var termHeight: Int

    @Published var connected = false
    private var intentionalDisconnect = false
    private var sessionId = ""
    private var grpcClient: GRPCClient<HTTP2ClientTransport.Posix>?
    private var clientRunTask: Task<Void, Never>?
    private var sessionTask: Task<Void, Never>?
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

        sessionTask = Task {
            do {
                let security: HTTP2ClientTransport.Posix.TransportSecurity = useTls
                    ? .tls
                    : .plaintext
                let transport = try HTTP2ClientTransport.Posix(
                    target: .dns(host: host, port: port),
                    transportSecurity: security
                )
                let client = GRPCClient(transport: transport)
                grpcClient = client

                clientRunTask = Task {
                    try? await client.runConnections()
                }

                let hydra = Hydra_HydraService.Client(wrapping: client)
                outputBuffer.removeAll(keepingCapacity: true)

                var authReq = Hydra_AuthRequest()
                authReq.username = username
                authReq.password = password
                let authResp = try await hydra.authenticate(authReq)

                guard authResp.success else {
                    let err = authResp.error.isEmpty ? "Authentication failed" : authResp.error
                    pushLine("[Hydra] \(err)")
                    return
                }
                sessionId = authResp.sessionID

                if !gameName.isEmpty {
                    var connReq = Hydra_ConnectRequest()
                    connReq.sessionID = sessionId
                    connReq.gameName = gameName
                    let connResp = try await hydra.connect(connReq, metadata: authMetadata())
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

                await runGameSession(hydra: hydra)

                if !intentionalDisconnect {
                    await attemptReconnect(hydra: hydra)
                }

            } catch {
                pushLine("[Hydra] Error: \(error.localizedDescription)")
            }

            connected = false
            grpcClient?.beginGracefulShutdown()
            clientRunTask = nil
            grpcClient = nil
            if !intentionalDisconnect {
                onDisconnect?()
            }
        }
    }

    func disconnect() {
        intentionalDisconnect = true
        connected = false
        inputContinuation?.finish()
        sessionTask?.cancel()
        grpcClient?.beginGracefulShutdown()
        outputBuffer.removeAll(keepingCapacity: true)
    }

    func sendLine(_ text: String) {
        guard connected else { return }
        var msg = Hydra_ClientMessage()
        msg.inputLine = text
        inputContinuation?.yield(msg)
        markActivity()
    }

    /// Update the terminal dimensions and send a fresh SetPreferences to the
    /// server.  Called when the output pane geometry changes (rotation,
    /// split-screen, etc.).
    func updateTerminalSize(width: Int, height: Int) {
        guard width != termWidth || height != termHeight else { return }
        termWidth = width
        termHeight = height
        guard inputContinuation != nil else { return }
        var prefs = Hydra_SetPreferences()
        prefs.colorFormat = .ansiTruecolor
        prefs.terminalWidth = UInt32(width)
        prefs.terminalHeight = UInt32(height)
        prefs.terminalType = "Titan-iOS"
        var msg = Hydra_ClientMessage()
        msg.preferences = prefs
        inputContinuation?.yield(msg)
    }

    // MARK: - GameSession Stream

    private func runGameSession(hydra: Hydra_HydraService.Client<HTTP2ClientTransport.Posix>,
                                fetchScrollbackOnOpen: Bool = false) async {
        let (inputStream, continuation) = AsyncStream<Hydra_ClientMessage>.makeStream()
        inputContinuation = continuation

        var prefs = Hydra_SetPreferences()
        prefs.colorFormat = .ansiTruecolor
        prefs.terminalWidth = UInt32(termWidth)
        prefs.terminalHeight = UInt32(termHeight)
        prefs.terminalType = "Titan-iOS"
        var prefsMsg = Hydra_ClientMessage()
        prefsMsg.preferences = prefs
        continuation.yield(prefsMsg)

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

        if fetchScrollbackOnOpen {
            await fetchScrollBack(hydra: hydra)
        }

        do {
            try await hydra.gameSession(metadata: authMetadata()) { writer in
                for await msg in inputStream {
                    try await writer.write(msg)
                }
            } onResponse: { [weak self] response in
                for try await msg in response.messages {
                    await self?.dispatchServerMessage(msg)
                }
            }
        } catch {
            if !intentionalDisconnect {
                pushLine("[Hydra] Stream error: \(error.localizedDescription)")
            }
        }

        pingTask.cancel()
        connected = false
    }

    private func attemptReconnect(hydra: Hydra_HydraService.Client<HTTP2ClientTransport.Posix>) async {
        guard !intentionalDisconnect, !sessionId.isEmpty else { return }

        for attempt in 1...Self.maxReconnectAttempts {
            pushLine("[Hydra] Stream lost, reconnecting (attempt \(attempt))...")
            try? await Task.sleep(nanoseconds: Self.reconnectDelay)

            guard !intentionalDisconnect else { return }

            connected = true
            outputBuffer.removeAll(keepingCapacity: true)
            markActivity()
            await runGameSession(hydra: hydra, fetchScrollbackOnOpen: true)

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
            pushLine(formatStructuredGmcp(package: gmcp.package, json: gmcp.json)
                ?? "[GMCP \(gmcp.package)] \(gmcp.json)")
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
        guard let client = grpcClient else { return "[Hydra] Not connected." }
        let hydra = Hydra_HydraService.Client(wrapping: client)
        do {
            var req = Hydra_ConnectRequest()
            req.sessionID = sessionId
            req.gameName = gameName
            let resp = try await hydra.connect(req, metadata: authMetadata())
            return resp.success
                ? "[Hydra] Connected to \(gameName) (link \(resp.linkNumber))"
                : "[Hydra] Connect failed: \(resp.error)"
        } catch { return "[Hydra] Error: \(error.localizedDescription)" }
    }

    func rpcSwitchLink(_ linkNumber: Int32) async -> String {
        guard let client = grpcClient else { return "[Hydra] Not connected." }
        let hydra = Hydra_HydraService.Client(wrapping: client)
        do {
            var req = Hydra_SwitchRequest()
            req.sessionID = sessionId
            req.linkNumber = linkNumber
            let resp = try await hydra.switchLink(req, metadata: authMetadata())
            return resp.success
                ? "[Hydra] Switched to link \(linkNumber)"
                : "[Hydra] Switch failed: \(resp.error)"
        } catch { return "[Hydra] Error: \(error.localizedDescription)" }
    }

    func rpcListLinks() async -> [String] {
        guard let client = grpcClient else { return ["[Hydra] Not connected."] }
        let hydra = Hydra_HydraService.Client(wrapping: client)
        do {
            var req = Hydra_SessionRequest()
            req.sessionID = sessionId
            let resp = try await hydra.listLinks(req, metadata: authMetadata())
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
        guard let client = grpcClient else { return "[Hydra] Not connected." }
        let hydra = Hydra_HydraService.Client(wrapping: client)
        do {
            var req = Hydra_DisconnectRequest()
            req.sessionID = sessionId
            req.linkNumber = linkNumber
            let resp = try await hydra.disconnectLink(req, metadata: authMetadata())
            return resp.success
                ? "[Hydra] Disconnected link \(linkNumber)"
                : "[Hydra] Disconnect failed: \(resp.error)"
        } catch { return "[Hydra] Error: \(error.localizedDescription)" }
    }

    func rpcGetSession() async -> [String] {
        guard let client = grpcClient else { return ["[Hydra] Not connected."] }
        let hydra = Hydra_HydraService.Client(wrapping: client)
        do {
            var req = Hydra_SessionRequest()
            req.sessionID = sessionId
            let resp = try await hydra.getSession(req, metadata: authMetadata())
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
        guard let client = grpcClient else { return "[Hydra] Not connected." }
        let hydra = Hydra_HydraService.Client(wrapping: client)
        do {
            var req = Hydra_SessionRequest()
            req.sessionID = sessionId
            _ = try await hydra.detachSession(req, metadata: authMetadata())
            connected = false
            return "[Hydra] Session detached. Reconnect to resume."
        } catch { return "[Hydra] Error: \(error.localizedDescription)" }
    }

    // MARK: - Helpers

    private func authMetadata() -> Metadata {
        var metadata = Metadata()
        metadata.addString(sessionId, forKey: "authorization")
        return metadata
    }

    private func fetchScrollBack(hydra: Hydra_HydraService.Client<HTTP2ClientTransport.Posix>) async {
        do {
            var req = Hydra_ScrollBackRequest()
            req.sessionID = sessionId
            req.maxLines = 200
            req.colorFormat = .ansiTruecolor
            let resp = try await hydra.getScrollBack(req, metadata: authMetadata())
            if !resp.lines.isEmpty {
                pushLine("-- scroll-back (\(resp.lines.count) lines) --")
                for line in resp.lines {
                    pushLine(line.text)
                }
                pushLine("-- end scroll-back --")
            }
        } catch {
            // Non-fatal — reconnect should proceed even if scroll-back fetch fails.
        }
    }

    private func formatStructuredGmcp(package: String, json: String) -> String? {
        guard package == "Char.Vitals", !json.isEmpty,
              let data = json.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            return nil
        }

        func pair(_ curKeys: [String], _ maxKeys: [String], label: String) -> String? {
            for cur in curKeys {
                for max in maxKeys {
                    if let curVal = obj[cur], let maxVal = obj[max] {
                        return "\(label) \(curVal)/\(maxVal)"
                    }
                }
            }
            return nil
        }

        let parts = [
            pair(["hp", "health"], ["maxhp", "max_health"], label: "HP"),
            pair(["mp", "mana"], ["maxmp", "maxmana"], label: "MP"),
            pair(["mv", "moves", "move"], ["maxmv", "maxmoves", "maxmove"], label: "MV"),
        ].compactMap { $0 }
        return parts.isEmpty ? nil : "[Vitals] " + parts.joined(separator: "  ")
    }

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

#endif // canImport(GRPCCore)
