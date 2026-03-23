import Foundation
import SwiftUI

// MARK: - World Tab (per-connection state)

@MainActor
@Observable
class WorldTab: Identifiable {
    let id = UUID()
    let name: String
    var connection: MudConnection?
    #if canImport(GRPC)
    var hydraConnection: HydraConnection?
    #endif
    var lines: [AttributedString] = []
    var history: [String] = []
    var hasActivity = false
    var disconnected = false
    var spawnLines: [String: [AttributedString]] = [:]
    var activeSpawn: String = ""  // "" = main, otherwise spawn path
    let mcpParser = McpParser()
    let variables = VariableStore()

    init(name: String) {
        self.name = name
    }

    var activeLines: [AttributedString] {
        if activeSpawn.isEmpty { return lines }
        return spawnLines[activeSpawn] ?? []
    }

    var isConnected: Bool {
        if connection?.connected == true { return true }
        #if canImport(GRPC)
        if hydraConnection?.connected == true { return true }
        #endif
        return false
    }

    func sendLine(_ text: String) {
        if let conn = connection, conn.connected {
            conn.sendLine(text)
        }
        #if canImport(GRPC)
        else if let hconn = hydraConnection, hconn.connected {
            hconn.sendLine(text)
        }
        #endif
    }

    func disconnectAll() {
        connection?.disconnect()
        #if canImport(GRPC)
        hydraConnection?.disconnect()
        #endif
    }
}

// MARK: - App State (global observable)

@MainActor
@Observable
class AppState {
    var tabs: [WorldTab] = [WorldTab(name: "(System)")]
    var activeTabIndex: Int = 0
    var showConnectSheet = false
    var showWorldManager = false
    var showTriggerManager = false
    var showSettings = false
    var showFindBar = false
    var findQuery = ""
    var findMatches: [Int] = []
    var findPos: Int = -1
    var logActive = false

    var activeTab: WorldTab? { tabs.indices.contains(activeTabIndex) ? tabs[activeTabIndex] : nil }

    let scrollbackLimit = 20000
    var spawnRepo = SpawnRepository()

    func appendLine(_ tabIndex: Int, _ line: String) {
        guard tabs.indices.contains(tabIndex) else { return }
        let parsed = AnsiParser.parse(line)

        // Main spawn always gets the line
        tabs[tabIndex].lines.append(parsed)
        while tabs[tabIndex].lines.count > scrollbackLimit {
            tabs[tabIndex].lines.removeFirst()
        }

        // Route to matching spawns
        let plain = AnsiParser.stripAnsi(line)
        for spawn in spawnRepo.load() {
            if spawn.matches(plain) {
                let display = spawn.prefix.isEmpty ? parsed : AnsiParser.parse("\(spawn.prefix)\(line)")
                if tabs[tabIndex].spawnLines[spawn.path] == nil {
                    tabs[tabIndex].spawnLines[spawn.path] = []
                }
                tabs[tabIndex].spawnLines[spawn.path]!.append(display)
                while tabs[tabIndex].spawnLines[spawn.path]!.count > spawn.maxLines {
                    tabs[tabIndex].spawnLines[spawn.path]!.removeFirst()
                }
            }
        }

        if tabIndex != activeTabIndex {
            let wasActive = tabs[tabIndex].hasActivity
            tabs[tabIndex].hasActivity = true
            if !wasActive {
                #if os(iOS)
                if UIApplication.shared.applicationState != .active {
                    let worldName = tabs[tabIndex].name
                    let plainLine = AnsiParser.stripAnsi(line)
                    BackgroundService.shared.postActivityNotification(worldName: worldName, line: plainLine)
                    let totalActivity = tabs.filter(\.hasActivity).count
                    BackgroundService.shared.updateBadge(count: totalActivity)
                }
                #endif
            }
        }
    }
}
