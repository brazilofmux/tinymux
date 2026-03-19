import Foundation
import Security

// MARK: - World Data Model

struct World: Codable, Identifiable {
    var id: String { name }
    var name: String
    var host: String
    var port: Int = 4201
    var ssl: Bool = false
    var character: String = ""
    var notes: String = ""
    var loginCommands: [String] = []
}

// MARK: - World Repository (Keychain-backed)

class WorldRepository {
    private static let service = "org.tinymux.titan.worlds"

    func load() -> [World] {
        guard let data = keychainRead(service: Self.service, account: "worlds") else {
            return []
        }
        return (try? JSONDecoder().decode([World].self, from: data)) ?? []
    }

    func save(_ worlds: [World]) {
        guard let data = try? JSONEncoder().encode(worlds) else { return }
        keychainWrite(service: Self.service, account: "worlds", data: data)
    }

    func add(_ world: World) {
        var worlds = load()
        if let idx = worlds.firstIndex(where: { $0.name == world.name }) {
            worlds[idx] = world
        } else {
            worlds.append(world)
        }
        save(worlds)
    }

    func remove(_ name: String) {
        save(load().filter { $0.name != name })
    }

    func get(_ name: String) -> World? {
        load().first { $0.name == name }
    }

    // MARK: - Keychain Helpers

    private func keychainRead(service: String, account: String) -> Data? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var result: AnyObject?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        return status == errSecSuccess ? result as? Data : nil
    }

    private func keychainWrite(service: String, account: String, data: Data) {
        // Delete existing
        let deleteQuery: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        SecItemDelete(deleteQuery as CFDictionary)

        // Add new
        let addQuery: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecValueData as String: data,
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlock,
        ]
        SecItemAdd(addQuery as CFDictionary, nil)
    }
}
