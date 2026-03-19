import Foundation
import Security
import CryptoKit

// MARK: - TOFU Certificate Store (Keychain-backed)

class TofuCertStore {
    private static let service = "org.tinymux.titan.certs"

    func getFingerprint(host: String, port: Int) -> String? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: Self.service,
            kSecAttrAccount as String: "\(host):\(port)",
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var result: AnyObject?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status == errSecSuccess, let data = result as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    func saveFingerprint(host: String, port: Int, fingerprint: String) {
        let account = "\(host):\(port)"
        // Delete existing
        SecItemDelete([
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: Self.service,
            kSecAttrAccount as String: account,
        ] as CFDictionary)
        // Add
        guard let data = fingerprint.data(using: .utf8) else { return }
        SecItemAdd([
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: Self.service,
            kSecAttrAccount as String: account,
            kSecValueData as String: data,
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlock,
        ] as CFDictionary, nil)
    }

    func removeFingerprint(host: String, port: Int) {
        SecItemDelete([
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: Self.service,
            kSecAttrAccount as String: "\(host):\(port)",
        ] as CFDictionary)
    }

    static func fingerprint(_ certData: Data) -> String {
        let hash = SHA256.hash(data: certData)
        return hash.map { String(format: "%02X", $0) }.joined(separator: ":")
    }
}
