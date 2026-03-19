import Foundation

// MARK: - Hook Data Model

struct Hook: Codable, Identifiable {
    var id: String { name }
    var name: String
    var event: String    // CONNECT, DISCONNECT, ACTIVITY
    var body: String
    var enabled: Bool = true

    static let events = ["CONNECT", "DISCONNECT", "ACTIVITY"]
}

// MARK: - Hook Repository

class HookRepository {
    private let key = "titan_hooks"

    func load() -> [Hook] {
        guard let data = UserDefaults.standard.data(forKey: key) else { return [] }
        return (try? JSONDecoder().decode([Hook].self, from: data)) ?? []
    }

    func save(_ hooks: [Hook]) {
        let data = try? JSONEncoder().encode(hooks)
        UserDefaults.standard.set(data, forKey: key)
    }

    func add(_ hook: Hook) {
        var list = load()
        if let idx = list.firstIndex(where: { $0.name == hook.name }) {
            list[idx] = hook
        } else {
            list.append(hook)
        }
        save(list)
    }

    func remove(_ name: String) {
        save(load().filter { $0.name != name })
    }

    func fireEvent(_ event: String) -> [String] {
        load().filter { $0.enabled && $0.event.caseInsensitiveCompare(event) == .orderedSame }
              .map(\.body)
    }
}
