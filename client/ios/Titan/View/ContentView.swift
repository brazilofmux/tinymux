import SwiftUI

struct ContentView: View {
    @Bindable var state: AppState
    @FocusState private var inputFocused: Bool
    @State private var inputText = ""
    @State private var historyPos = -1
    @State private var savedInput = ""

    var body: some View {
        VStack(spacing: 0) {
            // Toolbar
            toolbar

            // Tab bar
            tabBar

            // Find bar (conditional)
            if state.showFindBar {
                findBar
            }

            // Output pane
            outputPane

            // Input bar
            inputBar

            // Status bar
            statusBar
        }
        .background(Color.black)
        .onAppear {
            state.appendLine(0, "Titan for iOS")
            state.appendLine(0, "Tap Connect or Worlds to get started.")
            inputFocused = true
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
                            Circle()
                                .fill(Color.yellow)
                                .frame(width: 6, height: 6)
                        }
                        Text(tab.name)
                            .font(.system(size: 12))
                            .foregroundColor(
                                tab.disconnected ? Color(white: 0.4) :
                                index == state.activeTabIndex ? .white :
                                Color(white: 0.63)
                            )
                        // Close button (not on System tab)
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

                    // Separator
                    Rectangle()
                        .fill(Color(white: 0.3))
                        .frame(width: 1, height: 20)
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
                    .font(.system(size: 11))
                    .foregroundColor(Color(white: 0.63))
            } else if !state.findQuery.isEmpty {
                Text("0/0")
                    .font(.system(size: 11))
                    .foregroundColor(Color(white: 0.63))
            }

            toolbarButton("\u{25B2}") { findPrev() }
            toolbarButton("\u{25BC}") { findNext() }
            toolbarButton("\u{2715}") {
                state.showFindBar = false
                state.findQuery = ""
                state.findMatches = []
                state.findPos = -1
            }
        }
        .padding(.horizontal, 4)
        .padding(.vertical, 2)
        .background(Color(red: 0.1, green: 0.1, blue: 0.18))
    }

    // MARK: - Output Pane

    private var outputPane: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    let lines = state.activeTab?.lines ?? []
                    ForEach(Array(lines.enumerated()), id: \.offset) { index, line in
                        Text(line)
                            .font(.system(size: 14, design: .monospaced))
                            .foregroundColor(Color(white: 0.75))
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .id(index)
                    }
                }
                .padding(.horizontal, 4)
            }
            .textSelection(.enabled)
            .onChange(of: state.activeTab?.lines.count) { _, newCount in
                if let count = newCount, count > 0 {
                    withAnimation {
                        proxy.scrollTo(count - 1, anchor: .bottom)
                    }
                }
            }
        }
    }

    // MARK: - Input Bar

    private var inputBar: some View {
        HStack(spacing: 4) {
            TextField("", text: $inputText)
                .textFieldStyle(.plain)
                .font(.system(size: 14, design: .monospaced))
                .foregroundColor(.white)
                .focused($inputFocused)
                .onSubmit { handleInput() }
                .onKeyPress(.upArrow) { historyBack(); return .handled }
                .onKeyPress(.downArrow) { historyForward(); return .handled }

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
        .padding(.horizontal, 8)
        .padding(.vertical, 2)
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

    // MARK: - Actions

    private func handleInput() {
        let text = inputText.trimmingCharacters(in: .whitespaces)
        guard !text.isEmpty else { return }

        if let tab = state.activeTab {
            tab.history.insert(text, at: 0)
            if tab.history.count > 500 { tab.history.removeLast() }
        }
        historyPos = -1

        // Slash commands
        if text.hasPrefix("/") {
            let trimmed = String(text.dropFirst())
            let parts = trimmed.split(separator: " ", maxSplits: 1)
            let cmd = String(parts.first ?? "").lowercased()
            let args = parts.count > 1 ? String(parts[1]) : ""
            handleCommand(cmd, args: args)
            inputText = ""
            return
        }

        // Normal input
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

    private func handleCommand(_ cmd: String, args: String) {
        let idx = state.activeTabIndex
        switch cmd {
        case "connect":
            let parts = args.split(separator: " ")
            if parts.isEmpty {
                state.showConnectSheet = true
            } else {
                let host = String(parts[0])
                let port = parts.count > 1 ? Int(parts[1]) ?? 4201 : 4201
                let ssl = parts.contains(where: { $0.lowercased() == "ssl" || $0.lowercased() == "tls" })
                connectWorld(name: "\(host):\(port)", host: host, port: port, ssl: ssl)
            }
        case "dc", "disconnect":
            disconnectActive()
        case "worlds":
            state.showWorldManager = true
        case "triggers", "trig":
            state.showTriggerManager = true
        case "find":
            state.showFindBar = true
            if !args.isEmpty { state.findQuery = args }
        case "clear":
            state.activeTab?.lines.removeAll()
        case "help":
            state.appendLine(idx, "% Commands:")
            state.appendLine(idx, "%   /connect <host> [port] [ssl]  - Connect to a world")
            state.appendLine(idx, "%   /disconnect, /dc              - Close current connection")
            state.appendLine(idx, "%   /worlds                       - Open World Manager")
            state.appendLine(idx, "%   /triggers                     - Open Trigger Manager")
            state.appendLine(idx, "%   /find <text>                  - Search scrollback")
            state.appendLine(idx, "%   /clear                        - Clear scrollback")
            state.appendLine(idx, "%   /help                         - Show this help")
        default:
            state.appendLine(idx, "% Unknown command: /\(cmd)  (try /help)")
        }
    }

    func connectWorld(name: String, host: String, port: Int, ssl: Bool, loginCommands: [String] = []) {
        let tab = WorldTab(name: name)
        state.tabs.append(tab)
        let tabIndex = state.tabs.count - 1
        state.activeTabIndex = tabIndex

        let conn = MudConnection(name: name, host: host, port: port, useSsl: ssl)
        tab.connection = conn
        conn.onLine = { line in
            state.appendLine(tabIndex, line)
        }
        conn.onConnect = {
            state.appendLine(tabIndex, "% Connected to \(host):\(port)")
            tab.disconnected = false
            for cmd in loginCommands { conn.sendLine(cmd) }
        }
        conn.onDisconnect = {
            state.appendLine(tabIndex, "% Connection lost.")
            tab.disconnected = true
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

    // MARK: - History

    private func historyBack() {
        guard let tab = state.activeTab, !tab.history.isEmpty else { return }
        if historyPos < 0 { savedInput = inputText }
        let next = min(historyPos + 1, tab.history.count - 1)
        historyPos = next
        inputText = tab.history[next]
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
        var matches: [Int] = []
        for (i, line) in lines.enumerated() {
            if String(line.characters).lowercased().contains(query) {
                matches.append(i)
            }
        }
        state.findMatches = matches
        state.findPos = matches.isEmpty ? -1 : matches.count - 1
    }

    private func findPrev() {
        guard !state.findMatches.isEmpty else { return }
        state.findPos = state.findPos > 0 ? state.findPos - 1 : state.findMatches.count - 1
    }

    private func findNext() {
        guard !state.findMatches.isEmpty else { return }
        state.findPos = state.findPos < state.findMatches.count - 1 ? state.findPos + 1 : 0
    }

    // MARK: - Toolbar Button Helper

    private func toolbarButton(_ title: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 12))
                .foregroundColor(Color(white: 0.82))
        }
        .buttonStyle(.plain)
        .padding(.horizontal, 8)
        .frame(height: 28)
    }
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
