import SwiftUI

struct McpEditorSheet: View {
    let name: String
    let content: String
    var onSave: (String) -> Void
    var onDismiss: () -> Void

    @State private var text: String = ""

    var body: some View {
        NavigationStack {
            TextEditor(text: $text)
                .font(.system(size: 14, design: .monospaced))
                .scrollContentBackground(.hidden)
                .background(Color(white: 0.1))
                .foregroundColor(.white)
                .padding(4)
                .navigationTitle("Edit: \(name)")
                #if os(iOS)
                .navigationBarTitleDisplayMode(.inline)
                #endif
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button("Cancel") { onDismiss() }
                    }
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Save") { onSave(text) }
                    }
                }
                .onAppear { text = content }
        }
    }
}
