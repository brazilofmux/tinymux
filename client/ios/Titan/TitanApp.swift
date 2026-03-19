import SwiftUI

@main
struct TitanApp: App {
    @State private var appState = AppState()

    var body: some Scene {
        WindowGroup {
            ContentView(state: appState)
                .preferredColorScheme(.dark)
        }
    }
}
