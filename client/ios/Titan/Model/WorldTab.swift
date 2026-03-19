import Foundation
import SwiftUI

// MARK: - World Tab (per-connection state)

@MainActor
@Observable
class WorldTab: Identifiable {
    let id = UUID()
    let name: String
    var connection: MudConnection?
    var lines: [AttributedString] = []
    var history: [String] = []
    var hasActivity = false
    var disconnected = false

    init(name: String) {
        self.name = name
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

    func appendLine(_ tabIndex: Int, _ line: String) {
        guard tabs.indices.contains(tabIndex) else { return }
        let parsed = AnsiParser.parse(line)
        tabs[tabIndex].lines.append(parsed)
        while tabs[tabIndex].lines.count > scrollbackLimit {
            tabs[tabIndex].lines.removeFirst()
        }
        if tabIndex != activeTabIndex {
            tabs[tabIndex].hasActivity = true
        }
    }
}
