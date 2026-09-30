import Foundation
import Security
#if canImport(IOSLabDashboardCore)
import IOSLabDashboardCore
#endif

/// The API token, one Keychain item per backend address. It is never written to UserDefaults or to a URL.
final class KeychainTokenStore: TokenStore, @unchecked Sendable {
    private let service: String

    init(service: String = "com.ioslab.dashboard.api-token") {
        self.service = service
    }

    private func query(_ baseURL: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service, kSecAttrAccount as String: baseURL]
    }

    func token(for baseURL: String) -> String? {
        var q = query(baseURL)
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &item) == errSecSuccess, let data = item as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    func setToken(_ token: String?, for baseURL: String) {
        SecItemDelete(query(baseURL) as CFDictionary)
        guard let token, !token.isEmpty, let data = token.data(using: .utf8) else { return }
        var q = query(baseURL)
        q[kSecValueData as String] = data
        q[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlocked
        SecItemAdd(q as CFDictionary, nil)
    }
}
