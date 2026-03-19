import SwiftUI

struct ContentView: View {
    @Bindable var state: AppState
    @FocusState private var inputFocused: Bool
    @State private var inputText = ""
    @State private var historyPos = -1
    @State private var savedInput = ""

    // Subsystems
    let worldRepo = WorldRepository()
    let worldRepo = WorldRepository()
    let triggerRepo = TriggerRepository()
    let hookRepo = HookRepository()
    let settings = AppSettings()
    let certStore = TofuCertStore()
    @State private var triggerEngine = TriggerEngine()
    @State private var timerEngine = TimerEngine()
    @State private var sessionLogger = SessionLogger()
    @State private var pendingCert: (CertInfo, CheckedContinuation<Bool, Never>)? = nil
    @State private var mcpEditState: McpEditState? = nil

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            tabBar
            spawnBar
            if state.showFindBar { findBar }
            outputPane
            inputBar
            statusBar
        }
        .background(Color.black)
        .onAppear {
            state.appendLine(0, "Titan for iOS")
            state.appendLine(0, "Tap Connect or Worlds to get started.")
            triggerEngine.load(triggerRepo.load())
            timerEngine.onFire = { _, command in
                if let conn = state.activeTab?.connection, conn.connected {
                    conn.sendLine(command)
                }
            }
            inputFocused = true
        }
        .sheet(isPresented: $state.showConnectSheet) {
            ConnectSheet(isPresented: $state.showConnectSheet) { host, port, ssl in
                connectWorld(name: "\(host):\(port)", host: host, port: port, ssl: ssl)
            }
        }
        .sheet(isPresented: $state.showWorldManager) {
            WorldManagerView(worldRepo: worldRepo) { world in
                connectWorld(name: world.name, host: world.host, port: world.port,
                             ssl: world.ssl, loginCommands: world.loginCommands)
            }
        }
        .sheet(isPresented: $state.showTriggerManager) {
            TriggerManagerView(triggerRepo: triggerRepo) {
                triggerEngine.load(triggerRepo.load())
            }
        }
        .sheet(isPresented: $state.showSettings) {
            SettingsView(settings: settings)
        }
        .sheet(item: $mcpEditState) { edit in
            McpEditorSheet(name: edit.name, content: edit.content) { newContent in
                state.tabs[safe: edit.tabIndex]?.mcpParser.sendSimpleEditSet(
                    reference: edit.reference, type: edit.type, content: newContent)
                state.appendLine(edit.tabIndex, "% MCP edit saved: \(edit.name)")
                mcpEditState = nil
            } onDismiss: {
                mcpEditState = nil
            }
        }
        .sheet(item: Binding(
            get: { pendingCert.map { CertSheetItem(info: $0.0) } },
            set: { if $0 == nil, let (_, cont) = pendingCert { pendingCert = nil; cont.resume(returning: false) } }
        )) { item in
            CertVerifySheet(
                certInfo: item.info,
                onAccept: {
                    if let (_, cont) = pendingCert { pendingCert = nil; cont.resume(returning: true) }
                },
                onReject: {
                    if let (_, cont) = pendingCert { pendingCert = nil; cont.resume(returning: false) }
                }
            )
        }
        .onChange(of: settings.keepScreenOn) { _, newValue in
            #if os(iOS)
            UIApplication.shared.isIdleTimerDisabled = newValue
            #endif
        }
    }

    // MARK: - Toolbar

    private var toolbar: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 4) {
                toolbarButton("Connect") { state.showConnectSheet = true }
                toolbarButton("Worlds") { state.showWorldManager = true }
                toolbarButton("Trig") { state.showTriggerManager = true }
                toolbarButton("Find") { state.showFindBar.toggle() }
                toolbarButton("Cfg") { state.showSettings = true }
                toolbarButton("DC") { disconnectActive() }
                Spacer()
                toolbarButton("Clear") { state.activeTab?.lines.removeAll() }
            }
            .padding(.horizontal, 4)
            .padding(.vertical, 2)
        }
        .background(Color(white: 0.15))
    }

    // MARK: - Tab Bar

    private var tabBar: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 0) {
                ForEach(Array(state.tabs.enumerated()), id: \.element.id) { index, tab in
                    HStack(spacing: 4) {
                        if tab.hasActivity && index != state.activeTabIndex {
                            Circle().fill(Color.yellow).frame(width: 6, height: 6)
                        }
                        Text(tab.name)
                            .font(.system(size: 12))
                            .foregroundColor(
                                tab.disconnected ? Color(white: 0.4) :
                                index == state.activeTabIndex ? .white :
                                Color(white: 0.63)
                            )
                        if index > 0 {
                            Text("\u{2715}")
                                .font(.system(size: 10))
                                .foregroundColor(Color(white: 0.5))
                                .onTapGesture { closeTab(index) }
                        }
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(index == state.activeTabIndex ? Color.black : Color.clear)
                    .onTapGesture {
                        state.activeTabIndex = index
                        state.tabs[index].hasActivity = false
                    }
                    Rectangle().fill(Color(white: 0.3)).frame(width: 1, height: 20)
                }
            }
        }
        .frame(height: 36)
        .background(Color(white: 0.19))
    }

    // MARK: - Find Bar

    private var findBar: some View {
        HStack(spacing: 4) {
            TextField("Search...", text: $state.findQuery)
                .textFieldStyle(.plain)
                .font(.system(size: 12, design: .monospaced))
                .foregroundColor(.white)
                .onSubmit { updateFindMatches() }
            if !state.findMatches.isEmpty {
                Text("\(state.findPos + 1)/\(state.findMatches.count)")
                    .font(.system(size: 11)).foregroundColor(Color(white: 0.63))
            } else if !state.findQuery.isEmpty {
                Text("0/0").font(.system(size: 11)).foregroundColor(Color(white: 0.63))
            }
            toolbarButton("\u{25B2}") { findPrev() }
            toolbarButton("\u{25BC}") { findNext() }
            toolbarButton("\u{2715}") {
                state.showFindBar = false; state.findQuery = ""
                state.findMatches = []; state.findPos = -1
            }
        }
        .padding(.horizontal, 4).padding(.vertical, 2)
        .background(Color(red: 0.1, green: 0.1, blue: 0.18))
    }

    // MARK: - Spawn Selector

    private var spawnBar: some View {
        let spawns = state.spawnRepo.load()
        return Group {
            if !spawns.isEmpty && state.activeTabIndex > 0 {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 0) {
                        let tab = state.activeTab
                        let isMain = tab?.activeSpawn.isEmpty == true
                        Button { tab?.activeSpawn = "" } label: {
                            Text("Main")
                                .font(.system(size: 11))
                                .foregroundColor(isMain ? .white : Color(red: 0.5, green: 0.63, blue: 0.5))
                                .padding(.horizontal, 10)
                                .padding(.vertical, 4)
                                .background(isMain ? Color(red: 0.16, green: 0.29, blue: 0.16) : .clear)
                        }
                        .buttonStyle(.plain)

                        ForEach(spawns) { spawn in
                            let isActive = tab?.activeSpawn == spawn.path
                            let hasContent = !(tab?.spawnLines[spawn.path]?.isEmpty ?? true)
                            Button { tab?.activeSpawn = spawn.path } label: {
                                Text(spawn.name)
                                    .font(.system(size: 11))
                                    .foregroundColor(
                                        isActive ? .white :
                                        hasContent ? Color(red: 0.63, green: 0.75, blue: 0.63) :
                                        Color(red: 0.38, green: 0.44, blue: 0.38)
                                    )
                                    .padding(.horizontal, 10)
                                    .padding(.vertical, 4)
                                    .background(isActive ? Color(red: 0.16, green: 0.29, blue: 0.16) : .clear)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
                .frame(height: 28)
                .background(Color(red: 0.1, green: 0.17, blue: 0.1))
            }
        }
    }

    // MARK: - Output Pane

    private var outputPane: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    let lines = state.activeTab?.activeLines ?? []
                    ForEach(Array(lines.enumerated()), id: \.offset) { index, line in
                        Text(line)
                            .font(.system(size: CGFloat(settings.fontSize), design: .monospaced))
                            .foregroundColor(Color(white: 0.75))
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .id(index)
                    }
                }
                .padding(.horizontal, 4)
            }
            .textSelection(.enabled)
            .onChange(of: state.activeTab?.activeLines.count) { _, newCount in
                if let count = newCount, count > 0 {
                    withAnimation { proxy.scrollTo(count - 1, anchor: .bottom) }
                }
            }
        }
    }

    // MARK: - Input Bar

    private var inputBar: some View {
        HStack(spacing: 4) {
            TextField("", text: $inputText)
                .textFieldStyle(.plain)
                .font(.system(size: CGFloat(settings.fontSize), design: .monospaced))
                .foregroundColor(.white)
                .focused($inputFocused)
                .onSubmit { handleInput() }
                .onKeyPress(.upArrow) { historyBack(); return .handled }
                .onKeyPress(.downArrow) { historyForward(); return .handled }
                .onKeyPress(characters: "f", modifiers: .command) {
                    state.showFindBar.toggle(); return .handled
                }
                .onKeyPress(characters: "l", modifiers: .command) {
                    state.activeTab?.lines.removeAll(); return .handled
                }
            toolbarButton("Send") { handleInput() }
        }
        .padding(4)
        .background(Color(white: 0.13))
    }

    // MARK: - Status Bar

    private var statusBar: some View {
        HStack {
            Text(statusText)
                .font(.system(size: 11, design: .monospaced))
                .foregroundColor(.white)
            Spacer()
        }
        .padding(.horizontal, 8).padding(.vertical, 2)
        .background(Color(red: 0, green: 0, blue: 0.5))
    }

    private var statusText: String {
        guard let tab = state.activeTab else { return "(no connection)" }
        var s = tab.name
        if let conn = tab.connection {
            if conn.useSsl { s += " [ssl]" }
            if !conn.connected { s += " (disconnected)" }
        }
        if state.logActive { s += " [log]" }
        return s
    }

    // MARK: - Server Line Processing (triggers)

    private func processServerLine(_ tabIndex: Int, _ line: String) {
        // MCP intercept
        if let tab = state.tabs[safe: tabIndex], tab.mcpParser.processLine(line) { return }

        let result = triggerEngine.check(AnsiParser.stripAnsi(line))
        if result.gagged { return }
        let display = result.displayLine ?? line
        state.appendLine(tabIndex, display)
        if let conn = state.tabs[safe: tabIndex]?.connection, conn.connected {
            for cmd in result.commands { conn.sendLine(cmd) }
        }
        if sessionLogger.active && tabIndex == state.activeTabIndex {
            sessionLogger.writeLine(AnsiParser.stripAnsi(line))
        }
    }

    // MARK: - Connect

    func connectWorld(name: String, host: String, port: Int, ssl: Bool, loginCommands: [String] = []) {
        let tab = WorldTab(name: name)
        state.tabs.append(tab)
        let tabIndex = state.tabs.count - 1
        state.activeTabIndex = tabIndex

        let conn = MudConnection(name: name, host: host, port: port, useSsl: ssl, certStore: certStore)
        tab.connection = conn

        // Wire MCP
        tab.mcpParser.sendRaw = { raw in conn.sendLine(raw) }
        tab.mcpParser.onEditRequest = { reference, editName, type, content in
            mcpEditState = McpEditState(reference: reference, name: editName, type: type, content: content, tabIndex: tabIndex)
        }

        conn.onCertVerify = { certInfo in
            await withCheckedContinuation { continuation in
                pendingCert = (certInfo, continuation)
            }
        }
        conn.onLine = { line in processServerLine(tabIndex, line) }
        conn.onConnect = {
            state.appendLine(tabIndex, "% Connected to \(host):\(port)")
            tab.disconnected = false
            for cmd in hookRepo.fireEvent("CONNECT") { conn.sendLine(cmd) }
            for cmd in loginCommands { conn.sendLine(cmd) }
        }
        conn.onDisconnect = {
            state.appendLine(tabIndex, "% Connection lost.")
            tab.disconnected = true
            timerEngine.cancelAll()
            for cmd in hookRepo.fireEvent("DISCONNECT") {
                state.appendLine(tabIndex, "% [hook] \(cmd)")
            }
        }
        state.appendLine(tabIndex, "% Connecting to \(host):\(port)\(ssl ? " (ssl)" : "")...")
        conn.connect()
    }

    private func disconnectActive() {
        guard state.activeTabIndex > 0 else { return }
        state.tabs[state.activeTabIndex].connection?.disconnect()
        state.tabs.remove(at: state.activeTabIndex)
        state.activeTabIndex = max(state.activeTabIndex - 1, 0)
    }

    private func closeTab(_ index: Int) {
        guard index > 0 else { return }
        state.tabs[index].connection?.disconnect()
        state.tabs.remove(at: index)
        if state.activeTabIndex >= state.tabs.count {
            state.activeTabIndex = state.tabs.count - 1
        } else if state.activeTabIndex > index {
            state.activeTabIndex -= 1
        }
    }

    // MARK: - Input Handling

    private func handleInput() {
        let text = inputText.trimmingCharacters(in: .whitespaces)
        guard !text.isEmpty else { return }

        if let tab = state.activeTab {
            tab.history.insert(text, at: 0)
            if tab.history.count > 500 { tab.history.removeLast() }
        }
        historyPos = -1

        if text.hasPrefix("/") {
            let trimmed = String(text.dropFirst())
            let parts = trimmed.split(separator: " ", maxSplits: 1)
            let cmd = String(parts.first ?? "").lowercased()
            let args = parts.count > 1 ? String(parts[1]) : ""
            handleCommand(cmd, args: args)
            inputText = ""
            return
        }

        if let conn = state.activeTab?.connection, conn.connected {
            conn.sendLine(text)
            if !conn.telnet.remoteEcho {
                state.appendLine(state.activeTabIndex, "> \(text)")
            }
        } else {
            state.appendLine(state.activeTabIndex, "> \(text)")
        }
        inputText = ""
    }

    // MARK: - Commands

    private func handleCommand(_ cmd: String, args: String) {
        let idx = state.activeTabIndex
        switch cmd {
        case "connect":
            let parts = args.split(separator: " ")
            if parts.isEmpty { state.showConnectSheet = true }
            else {
                let host = String(parts[0])
                let port = parts.count > 1 ? Int(parts[1]) ?? settings.defaultPort : settings.defaultPort
                let ssl = parts.contains { $0.lowercased() == "ssl" || $0.lowercased() == "tls" }
                connectWorld(name: "\(host):\(port)", host: host, port: port, ssl: ssl)
            }
        case "dc", "disconnect":
            disconnectActive()
        case "worlds":
            state.showWorldManager = true
        case "triggers", "trig":
            state.showTriggerManager = true
        case "def":
            let eqPos = args.firstIndex(of: "=")
            guard let eqPos else {
                state.appendLine(idx, "% Usage: /def <name> <pattern> = <action>"); return
            }
            let before = args[args.startIndex..<eqPos].trimmingCharacters(in: .whitespaces)
                .split(separator: " ", maxSplits: 1)
            let body = args[args.index(after: eqPos)...].trimmingCharacters(in: .whitespaces)
            guard before.count >= 2 else {
                state.appendLine(idx, "% Usage: /def <name> <pattern> = <action>"); return
            }
            let tName = String(before[0]), pattern = String(before[1])
            do {
                _ = try Regex(pattern)
                triggerRepo.add(Trigger(name: tName, pattern: pattern, body: body))
                triggerEngine.load(triggerRepo.load())
                state.appendLine(idx, "% Trigger '\(tName)' defined: /\(pattern)/")
            } catch {
                state.appendLine(idx, "% Bad pattern: \(error.localizedDescription)")
            }
        case "undef":
            let name = args.trimmingCharacters(in: .whitespaces)
            guard !name.isEmpty else { state.appendLine(idx, "% Usage: /undef <name>"); return }
            triggerRepo.remove(name)
            triggerEngine.load(triggerRepo.load())
            state.appendLine(idx, "% Trigger '\(name)' removed.")
        case "find":
            state.showFindBar = true
            if !args.isEmpty { state.findQuery = args }
        case "log":
            if sessionLogger.active {
                let file = sessionLogger.currentFile()
                sessionLogger.stop()
                state.logActive = false
                state.appendLine(idx, "% Logging stopped. File: \(file?.lastPathComponent ?? "")")
            } else {
                let worldName = state.activeTab?.name ?? "system"
                let filename = args.trimmingCharacters(in: .whitespaces)
                let file = sessionLogger.start(worldName: worldName,
                                               filename: filename.isEmpty ? nil : filename)
                state.logActive = true
                state.appendLine(idx, "% Logging to: \(file.lastPathComponent)")
            }
        case "repeat":
            let parts = args.split(separator: " ", maxSplits: 2)
            guard parts.count >= 3, let seconds = Double(parts[1]), seconds > 0 else {
                state.appendLine(idx, "% Usage: /repeat <name> <seconds> <command>"); return
            }
            let tName = String(parts[0]), command = String(parts[2])
            timerEngine.add(name: tName, command: command, intervalSeconds: seconds)
            state.appendLine(idx, "% Timer '\(tName)' set: every \(seconds)s -> \(command)")
        case "killtimer", "cancel":
            let name = args.trimmingCharacters(in: .whitespaces)
            guard !name.isEmpty else { state.appendLine(idx, "% Usage: /killtimer <name>"); return }
            if timerEngine.remove(name) {
                state.appendLine(idx, "% Timer '\(name)' cancelled.")
            } else {
                state.appendLine(idx, "% No timer named '\(name)'.")
            }
        case "timers", "listtimers":
            let list = timerEngine.list()
            if list.isEmpty { state.appendLine(idx, "% No active timers.") }
            else {
                state.appendLine(idx, "% Active timers:")
                for t in list {
                    let shots = t.shotsRemaining < 0 ? "inf" : "\(t.shotsRemaining)"
                    state.appendLine(idx, "%   \(t.name): every \(t.intervalSeconds)s, shots=\(shots) -> \(t.command)")
                }
            }
        case "hook":
            let eqPos = args.firstIndex(of: "=")
            guard let eqPos else {
                state.appendLine(idx, "% Usage: /hook <name> <event> = <command>")
                state.appendLine(idx, "% Events: \(Hook.events.joined(separator: ", "))"); return
            }
            let before = args[args.startIndex..<eqPos].trimmingCharacters(in: .whitespaces)
                .split(separator: " ", maxSplits: 1)
            let body = args[args.index(after: eqPos)...].trimmingCharacters(in: .whitespaces)
            guard before.count >= 2 else {
                state.appendLine(idx, "% Usage: /hook <name> <event> = <command>"); return
            }
            let hName = String(before[0]), event = String(before[1]).uppercased()
            guard Hook.events.contains(event) else {
                state.appendLine(idx, "% Unknown event '\(event)'. Valid: \(Hook.events.joined(separator: ", "))"); return
            }
            hookRepo.add(Hook(name: hName, event: event, body: body))
            state.appendLine(idx, "% Hook '\(hName)' on \(event) -> \(body)")
        case "unhook":
            let name = args.trimmingCharacters(in: .whitespaces)
            guard !name.isEmpty else { state.appendLine(idx, "% Usage: /unhook <name>"); return }
            hookRepo.remove(name)
            state.appendLine(idx, "% Hook '\(name)' removed.")
        case "hooks", "listhooks":
            let list = hookRepo.load()
            if list.isEmpty { state.appendLine(idx, "% No hooks defined.") }
            else {
                state.appendLine(idx, "% Hooks:")
                for h in list {
                    state.appendLine(idx, "%   \(h.name): \(h.event) [\(h.enabled ? "on" : "off")] -> \(h.body)")
                }
            }
        case "spawn":
            let parts = args.split(separator: " ", maxSplits: 2).map(String.init)
            switch parts.first?.lowercased() {
            case "add":
                guard parts.count >= 3 else {
                    state.appendLine(idx, "% Usage: /spawn add <name> <pattern>"); break
                }
                let sName = parts[1], pattern = parts[2]
                do {
                    _ = try Regex(pattern)
                    state.spawnRepo.add(SpawnConfig(name: sName, path: sName.lowercased(), patterns: [pattern]))
                    state.appendLine(idx, "% Spawn '\(sName)' added: /\(pattern)/")
                } catch {
                    state.appendLine(idx, "% Bad pattern: \(error.localizedDescription)")
                }
            case "remove", "del":
                guard let sName = parts[safe: 1], !sName.isEmpty else {
                    state.appendLine(idx, "% Usage: /spawn remove <name>"); break
                }
                state.spawnRepo.remove(sName.lowercased())
                state.appendLine(idx, "% Spawn '\(sName)' removed.")
            case "list", .none:
                let list = state.spawnRepo.load()
                if list.isEmpty {
                    state.appendLine(idx, "% No spawns defined. Use /spawn add <name> <pattern>")
                } else {
                    state.appendLine(idx, "% Spawns:")
                    for s in list {
                        state.appendLine(idx, "%   \(s.name) (\(s.path)): \(s.patterns.map { "/\($0)/" }.joined(separator: ", "))")
                    }
                }
            case "focus", "fg":
                let sName = parts[safe: 1] ?? ""
                if sName.isEmpty || sName.lowercased() == "main" {
                    state.activeTab?.activeSpawn = ""
                } else {
                    state.activeTab?.activeSpawn = sName.lowercased()
                }
            default:
                state.appendLine(idx, "% Usage: /spawn [add|remove|list|focus] ...")
            }
        case "clear":
            state.activeTab?.lines.removeAll()
        case "help":
            state.appendLine(idx, "% Commands:")
            state.appendLine(idx, "%   /connect <host> [port] [ssl]  - Connect to a world")
            state.appendLine(idx, "%   /disconnect, /dc              - Close current connection")
            state.appendLine(idx, "%   /worlds                       - Open World Manager")
            state.appendLine(idx, "%   /triggers                     - Open Trigger Manager")
            state.appendLine(idx, "%   /def <name> <pattern> = <cmd> - Define a trigger")
            state.appendLine(idx, "%   /undef <name>                 - Remove a trigger")
            state.appendLine(idx, "%   /find <text>                  - Search scrollback")
            state.appendLine(idx, "%   /log [filename]               - Toggle session logging")
            state.appendLine(idx, "%   /repeat <name> <sec> <cmd>    - Create repeating timer")
            state.appendLine(idx, "%   /killtimer <name>             - Cancel a timer")
            state.appendLine(idx, "%   /timers                       - List active timers")
            state.appendLine(idx, "%   /hook <name> <event> = <cmd>  - Define event hook")
            state.appendLine(idx, "%   /unhook <name>                - Remove a hook")
            state.appendLine(idx, "%   /hooks                        - List hooks")
            state.appendLine(idx, "%   /spawn add <name> <pattern>  - Add output spawn")
            state.appendLine(idx, "%   /spawn remove <name>          - Remove spawn")
            state.appendLine(idx, "%   /spawn list                   - List spawns")
            state.appendLine(idx, "%   /spawn focus <name|main>      - Switch spawn view")
            state.appendLine(idx, "%   /clear                        - Clear scrollback")
            state.appendLine(idx, "%   /help                         - Show this help")
        default:
            state.appendLine(idx, "% Unknown command: /\(cmd)  (try /help)")
        }
    }

    // MARK: - History

    private func historyBack() {
        guard let tab = state.activeTab, !tab.history.isEmpty else { return }
        if historyPos < 0 { savedInput = inputText }
        historyPos = min(historyPos + 1, tab.history.count - 1)
        inputText = tab.history[historyPos]
    }

    private func historyForward() {
        guard historyPos >= 0 else { return }
        if historyPos > 0 {
            historyPos -= 1
            inputText = state.activeTab?.history[historyPos] ?? ""
        } else {
            historyPos = -1
            inputText = savedInput
        }
    }

    // MARK: - Find

    private func updateFindMatches() {
        guard !state.findQuery.isEmpty, let lines = state.activeTab?.lines else {
            state.findMatches = []; state.findPos = -1; return
        }
        let query = state.findQuery.lowercased()
        state.findMatches = lines.enumerated().compactMap { i, line in
            String(line.characters).lowercased().contains(query) ? i : nil
        }
        state.findPos = state.findMatches.isEmpty ? -1 : state.findMatches.count - 1
    }

    private func findPrev() {
        guard !state.findMatches.isEmpty else { return }
        state.findPos = state.findPos > 0 ? state.findPos - 1 : state.findMatches.count - 1
    }

    private func findNext() {
        guard !state.findMatches.isEmpty else { return }
        state.findPos = state.findPos < state.findMatches.count - 1 ? state.findPos + 1 : 0
    }

    // MARK: - Helpers

    private func toolbarButton(_ title: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title).font(.system(size: 12)).foregroundColor(Color(white: 0.82))
        }
        .buttonStyle(.plain)
        .padding(.horizontal, 8)
        .frame(height: 28)
    }
}

// Safe array subscript
extension Array {
    subscript(safe index: Index) -> Element? {
        indices.contains(index) ? self[index] : nil
    }
}

// Identifiable wrapper for cert sheet binding
struct CertSheetItem: Identifiable {
    let id = UUID()
    let info: CertInfo
}

// MARK: - Connect Sheet

struct ConnectSheet: View {
    @Binding var isPresented: Bool
    var onConnect: (String, Int, Bool) -> Void

    @State private var host = ""
    @State private var port = ""
    @State private var ssl = false

    var body: some View {
        NavigationStack {
            Form {
                TextField("Host", text: $host)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                TextField("Port", text: $port)
                    .keyboardType(.numberPad)
                Toggle("SSL/TLS", isOn: $ssl)
            }
            .navigationTitle("Connect")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { isPresented = false }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Connect") {
                        guard !host.isEmpty else { return }
                        isPresented = false
                        onConnect(host.trimmingCharacters(in: .whitespaces),
                                  Int(port) ?? 4201, ssl)
                    }
                }
            }
        }
        .presentationDetents([.medium])
    }
}
