import Foundation

// MARK: - Spawn Config

struct SpawnConfig: Codable, Identifiable {
    var id: String { path }
    var name: String
    var path: String
    var patterns: [String] = []
    var exceptions: [String] = []
    var prefix: String = ""
    var maxLines: Int = 20000
    var weight: Int = 0

    func matches(_ line: String) -> Bool {
        guard !patterns.isEmpty else { return false }
        let matchesPattern = patterns.contains { p in
            (try? Regex(p).ignoresCase().firstMatch(in: line)) != nil
        }
        guard matchesPattern else { return false }
        let matchesException = exceptions.contains { p in
            (try? Regex(p).ignoresCase().firstMatch(in: line)) != nil
        }
        return !matchesException
    }
}

// MARK: - Spawn Repository

class SpawnRepository {
    private let key = "titan_spawns"

    func load() -> [SpawnConfig] {
        guard let data = UserDefaults.standard.data(forKey: key) else { return [] }
        return ((try? JSONDecoder().decode([SpawnConfig].self, from: data)) ?? [])
            .sorted { $0.weight < $1.weight }
    }

    func save(_ spawns: [SpawnConfig]) {
        let data = try? JSONEncoder().encode(spawns)
        UserDefaults.standard.set(data, forKey: key)
    }

    func add(_ spawn: SpawnConfig) {
        var list = load()
        if let idx = list.firstIndex(where: { $0.path == spawn.path }) {
            list[idx] = spawn
        } else {
            list.append(spawn)
        }
        list.sort { $0.weight < $1.weight }
        save(list)
    }

    func remove(_ path: String) {
        save(load().filter { $0.path != path })
    }
}
