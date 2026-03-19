import Foundation
import UserNotifications
#if os(iOS)
import UIKit
import BackgroundTasks
#endif

// MARK: - Background Service

/// Manages app lifecycle for keeping connections alive and sending notifications.
/// On iOS, connections survive brief backgrounding (~30s) naturally via NWConnection.
/// For longer periods, we register a BGAppRefreshTask to periodically wake.
/// When activity arrives on a background tab, we post a local notification.
class BackgroundService {
    static let shared = BackgroundService()
    static let bgTaskId = "org.tinymux.titan.refresh"

    private init() {}

    // MARK: - Notification Permission

    func requestNotificationPermission() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .badge, .sound]) { _, _ in }
    }

    // MARK: - Background Task Registration (call from app init)

    func registerBackgroundTask() {
        #if os(iOS)
        BGTaskScheduler.shared.register(forTaskWithIdentifier: Self.bgTaskId, using: nil) { task in
            self.handleBackgroundRefresh(task as! BGAppRefreshTask)
        }
        #endif
    }

    // MARK: - Schedule Next Refresh

    func scheduleBackgroundRefresh() {
        #if os(iOS)
        let request = BGAppRefreshTaskRequest(identifier: Self.bgTaskId)
        request.earliestBeginDate = Date(timeIntervalSinceNow: 60) // 1 minute minimum
        try? BGTaskScheduler.shared.submit(request)
        #endif
    }

    // MARK: - Handle Background Wake

    #if os(iOS)
    private func handleBackgroundRefresh(_ task: BGAppRefreshTask) {
        // Schedule next refresh
        scheduleBackgroundRefresh()

        // NWConnection stays alive as long as the process runs.
        // This wake just keeps us in the scheduler's good graces.
        task.expirationHandler = {
            task.setTaskCompleted(success: false)
        }

        // Complete immediately — the connections are already running
        task.setTaskCompleted(success: true)
    }
    #endif

    // MARK: - Local Notifications

    func postActivityNotification(worldName: String, line: String) {
        let content = UNMutableNotificationContent()
        content.title = "Titan - \(worldName)"
        content.body = String(line.prefix(100))
        content.sound = .default

        let request = UNNotificationRequest(
            identifier: UUID().uuidString,
            content: content,
            trigger: nil // Deliver immediately
        )
        UNUserNotificationCenter.current().add(request)
    }

    // MARK: - Badge Management

    func updateBadge(count: Int) {
        #if os(iOS)
        Task { @MainActor in
            UNUserNotificationCenter.current().setBadgeCount(count)
        }
        #endif
    }

    func clearBadge() {
        updateBadge(count: 0)
    }
}
