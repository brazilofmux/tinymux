import SwiftUI

// MARK: - World Manager

struct WorldManagerView: View {
    @Environment(\.dismiss) var dismiss
    let worldRepo: WorldRepository
    var onConnect: (World) -> Void

    @State private var worlds: [World] = []
    @State private var editingWorld: World?
    @State private var showAdd = false
    @State private var deleteTarget: World?

    var body: some View {
        NavigationStack {
            List {
                if worlds.isEmpty {
                    Text("No saved worlds yet.")
                        .foregroundColor(.gray)
                } else {
                    ForEach(worlds) { world in
                        worldRow(world)
                    }
                    .onDelete { indices in
                        for i in indices {
                            worldRepo.remove(worlds[i].name)
                        }
                        refresh()
                    }
                }
            }
            .navigationTitle("Worlds")
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Close") { dismiss() }
                }
                ToolbarItem(placement: .primaryAction) {
                    Button("Add") { showAdd = true }
                }
            }
            .sheet(isPresented: $showAdd) {
                EditWorldView(title: "Add World") { world in
                    worldRepo.add(world)
                    refresh()
                }
            }
            .sheet(item: $editingWorld) { world in
                EditWorldView(title: "Edit World", initial: world) { updated in
                    if updated.name != world.name { worldRepo.remove(world.name) }
                    worldRepo.add(updated)
                    refresh()
                }
            }
            .onAppear { refresh() }
        }
    }

    private func worldRow(_ world: World) -> some View {
        Button {
            dismiss()
            onConnect(world)
        } label: {
            VStack(alignment: .leading, spacing: 2) {
                Text(world.name).fontWeight(.bold)
                HStack(spacing: 4) {
                    Text("\(world.host):\(world.port)")
                    if world.ssl { Text("(ssl)") }
                    if !world.character.isEmpty { Text("- \(world.character)") }
                    if !world.loginCommands.isEmpty { Text("[auto-login]") }
                }
                .font(.caption)
                .foregroundColor(.gray)
            }
        }
        .swipeActions(edge: .trailing) {
            Button(role: .destructive) {
                worldRepo.remove(world.name)
                refresh()
            } label: { Label("Delete", systemImage: "trash") }

            Button {
                editingWorld = world
            } label: { Label("Edit", systemImage: "pencil") }
            .tint(.blue)
        }
    }

    private func refresh() { worlds = worldRepo.load() }
}

// MARK: - Edit World

struct EditWorldView: View {
    @Environment(\.dismiss) var dismiss
    let title: String
    var initial: World?
    var onSave: (World) -> Void

    @State private var name = ""
    @State private var host = ""
    @State private var port = ""
    @State private var ssl = false
    @State private var character = ""
    @State private var notes = ""
    @State private var loginCmds = ""

    var body: some View {
        NavigationStack {
            Form {
                Section("Connection") {
                    TextField("Name", text: $name)
                    TextField("Host", text: $host)
                        .autocorrectionDisabled()
                        #if os(iOS)
                        .textInputAutocapitalization(.never)
                        #endif
                    TextField("Port", text: $port)
                        #if os(iOS)
                        .keyboardType(.numberPad)
                        #endif
                    Toggle("SSL/TLS", isOn: $ssl)
                }
                Section("Character") {
                    TextField("Character (optional)", text: $character)
                    TextField("Notes (optional)", text: $notes, axis: .vertical)
                        .lineLimit(3...5)
                }
                Section("Auto-Login") {
                    TextField("Commands (one per line)", text: $loginCmds, axis: .vertical)
                        .lineLimit(3...5)
                        .font(.system(.body, design: .monospaced))
                    Text("Sent automatically after connecting. Stored in Keychain.")
                        .font(.caption)
                        .foregroundColor(.gray)
                }
            }
            .navigationTitle(title)
            #if os(iOS)
            .navigationBarTitleDisplayMode(.inline)
            #endif
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        guard !name.isEmpty, !host.isEmpty else { return }
                        let world = World(
                            name: name.trimmingCharacters(in: .whitespaces),
                            host: host.trimmingCharacters(in: .whitespaces),
                            port: Int(port) ?? 4201,
                            ssl: ssl,
                            character: character.trimmingCharacters(in: .whitespaces),
                            notes: notes.trimmingCharacters(in: .whitespaces),
                            loginCommands: loginCmds.components(separatedBy: .newlines)
                                .map { $0.trimmingCharacters(in: .whitespaces) }
                                .filter { !$0.isEmpty }
                        )
                        dismiss()
                        onSave(world)
                    }
                }
            }
            .onAppear {
                if let w = initial {
                    name = w.name; host = w.host; port = String(w.port)
                    ssl = w.ssl; character = w.character; notes = w.notes
                    loginCmds = w.loginCommands.joined(separator: "\n")
                }
            }
        }
    }
}
