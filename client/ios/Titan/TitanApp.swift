import SwiftUI

@main
struct TitanApp: App {
    @State private var appState = AppState()

    init() {
        BackgroundService.shared.registerBackgroundTask()
        BackgroundService.shared.requestNotificationPermission()
    }

    var body: some Scene {
        WindowGroup {
            ContentView(state: appState)
                .preferredColorScheme(.dark)
                #if os(iOS)
                .onReceive(NotificationCenter.default.publisher(for: UIApplication.didEnterBackgroundNotification)) { _ in
                    BackgroundService.shared.scheduleBackgroundRefresh()
                }
                .onReceive(NotificationCenter.default.publisher(for: UIApplication.willEnterForegroundNotification)) { _ in
                    BackgroundService.shared.clearBadge()
                }
                #endif
        }
    }
}
