import Foundation

// MARK: - Trigger Data Model

struct Trigger: Codable, Identifiable {
    var id: String { name }
    var name: String
    var pattern: String
    var body: String = ""
    var priority: Int = 0
    var shots: Int = -1
    var gag: Bool = false
    var hilite: Bool = false
    var enabled: Bool = true
}

// MARK: - Trigger Repository

class TriggerRepository {
    private let key = "titan_triggers"

    func load() -> [Trigger] {
        guard let data = UserDefaults.standard.data(forKey: key) else { return [] }
        return (try? JSONDecoder().decode([Trigger].self, from: data)) ?? []
    }

    func save(_ triggers: [Trigger]) {
        let data = try? JSONEncoder().encode(triggers)
        UserDefaults.standard.set(data, forKey: key)
    }

    func add(_ trigger: Trigger) {
        var list = load()
        if let idx = list.firstIndex(where: { $0.name == trigger.name }) {
            list[idx] = trigger
        } else {
            list.append(trigger)
        }
        list.sort { $0.priority > $1.priority }
        save(list)
    }

    func remove(_ name: String) {
        save(load().filter { $0.name != name })
    }
}

// MARK: - Trigger Engine

struct TriggerResult {
    var matched = false
    var gagged = false
    var commands: [String] = []
    var hiliteLine: String? = nil
}

class TriggerEngine {
    private var compiled: [(Trigger, Regex<AnyRegexOutput>?)] = []

    func load(_ triggers: [Trigger]) {
        compiled = triggers.sorted { $0.priority > $1.priority }.map { t in
            let re: Regex<AnyRegexOutput>? = if t.enabled && !t.pattern.isEmpty {
                try? Regex(t.pattern).ignoresCase()
            } else {
                nil
            }
            return (t, re)
        }
    }

    func check(_ line: String) -> TriggerResult {
        var result = TriggerResult()

        for (trigger, regex) in compiled {
            guard let regex, trigger.shots != 0 else { continue }
            guard let match = try? regex.firstMatch(in: line) else { continue }

            result.matched = true
            if trigger.gag { result.gagged = true }

            if trigger.hilite && !result.gagged {
                let range = match.range
                let before = line[line.startIndex..<range.lowerBound]
                let matched = line[range]
                let after = line[range.upperBound..<line.endIndex]
                result.hiliteLine = "\(before)\u{1b}[1m\(matched)\u{1b}[22m\(after)"
            }

            if !trigger.body.isEmpty {
                var cmd = trigger.body
                // Substitute $0 with full match
                cmd = cmd.replacingOccurrences(of: "$0", with: String(match.output[0].substring ?? ""))
                result.commands.append(cmd)
            }
        }

        return result
    }
}
