import SwiftUI

struct SettingsView: View {
    @Environment(\.dismiss) var dismiss
    let settings: AppSettings

    @State private var fontSize: String = ""
    @State private var fontSizeLand: String = ""
    @State private var scrollback: String = ""
    @State private var defaultPort: String = ""
    @State private var defaultSsl = false
    @State private var keepScreenOn = false

    var body: some View {
        NavigationStack {
            Form {
                Section("Font") {
                    HStack {
                        TextField("Portrait size", text: $fontSize)
                            #if os(iOS)
                            .keyboardType(.numberPad)
                            #endif
                        TextField("Landscape size", text: $fontSizeLand)
                            #if os(iOS)
                            .keyboardType(.numberPad)
                            #endif
                    }
                }
                Section("Display") {
                    TextField("Scrollback lines", text: $scrollback)
                        #if os(iOS)
                        .keyboardType(.numberPad)
                        #endif
                    Toggle("Keep screen on", isOn: $keepScreenOn)
                }
                Section("Connection Defaults") {
                    TextField("Default port", text: $defaultPort)
                        #if os(iOS)
                        .keyboardType(.numberPad)
                        #endif
                    Toggle("Default SSL/TLS", isOn: $defaultSsl)
                }
            }
            .navigationTitle("Settings")
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        settings.fontSize = clamp(Int(fontSize) ?? 14, 8, 32)
                        settings.fontSizeLandscape = clamp(Int(fontSizeLand) ?? 12, 8, 32)
                        settings.scrollbackLines = clamp(Int(scrollback) ?? 20000, 1000, 100000)
                        settings.defaultPort = clamp(Int(defaultPort) ?? 4201, 1, 65535)
                        settings.defaultSsl = defaultSsl
                        settings.keepScreenOn = keepScreenOn
                        dismiss()
                    }
                }
            }
            .onAppear {
                fontSize = String(settings.fontSize)
                fontSizeLand = String(settings.fontSizeLandscape)
                scrollback = String(settings.scrollbackLines)
                defaultPort = String(settings.defaultPort)
                defaultSsl = settings.defaultSsl
                keepScreenOn = settings.keepScreenOn
            }
        }
    }

    private func clamp(_ value: Int, _ lo: Int, _ hi: Int) -> Int {
        min(max(value, lo), hi)
    }
}
