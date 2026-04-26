import Foundation

// MARK: - Variable Store

class VariableStore {
    // User-defined session variables (temp.*)
    var temp: [String: String] = [:]

    // Per-world session variables (worldtemp.*)
    var worldTemp: [String: String] = [:]

    // Last regex match captures (regexp.*)
    var regexpCaptures: [String] = []

    // Resolve a variable reference like "world.name"
    func resolve(_ key: String, worldName: String = "", character: String = "",
                 host: String = "", port: Int = 0, connected: Bool = false,
                 eventLine: String = "", eventCause: String = "line") -> String? {
        let parts = key.split(separator: ".", maxSplits: 1).map(String.init)
        guard parts.count == 2 else { return temp[key] }

        let namespace = parts[0]
        let name = parts[1]

        switch namespace {
        case "world":
            switch name {
            case "name": return worldName
            case "character": return character
            case "host": return host
            case "port": return "\(port)"
            case "connected": return connected ? "1" : "0"
            default: return nil
            }
        case "event":
            switch name {
            case "line": return eventLine
            case "cause": return eventCause
            default: return nil
            }
        case "regexp":
            guard let idx = Int(name) else { return nil }
            return idx < regexpCaptures.count ? regexpCaptures[idx] : nil
        case "datetime":
            return resolveDatetime(name)
        case "temp":
            return temp[name]
        case "worldtemp":
            return worldTemp[name]
        default:
            return nil
        }
    }

    private func resolveDatetime(_ name: String) -> String? {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US")
        switch name {
        case "date": formatter.dateFormat = "yyyy-MM-dd"
        case "time": formatter.dateFormat = "HH:mm:ss"
        case "year": formatter.dateFormat = "yyyy"
        case "month": formatter.dateFormat = "MM"
        case "day": formatter.dateFormat = "dd"
        case "hour": formatter.dateFormat = "HH"
        case "minute": formatter.dateFormat = "mm"
        case "second": formatter.dateFormat = "ss"
        case "weekday": formatter.dateFormat = "EEEE"
        case "weekdayshort": formatter.dateFormat = "EEE"
        case "monthname": formatter.dateFormat = "MMMM"
        case "monthnameshort": formatter.dateFormat = "MMM"
        default: return nil
        }
        return formatter.string(from: Date())
    }

    // Expand $var.name references in a string
    func expand(_ text: String, worldName: String = "", character: String = "",
                host: String = "", port: Int = 0, connected: Bool = false,
                eventLine: String = "", eventCause: String = "line") -> String {
        text.replacing(#/\$([a-zA-Z_][a-zA-Z0-9_]*(?:\.[a-zA-Z0-9_]+)*)/#) { match in
            resolve(String(match.output.1), worldName: worldName, character: character,
                    host: host, port: port, connected: connected,
                    eventLine: eventLine, eventCause: eventCause) ?? String(match.output.0)
        }
    }
}
