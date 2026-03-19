import SwiftUI

// MARK: - Trigger Manager

struct TriggerManagerView: View {
    @Environment(\.dismiss) var dismiss
    let triggerRepo: TriggerRepository
    var onChanged: () -> Void

    @State private var triggers: [Trigger] = []
    @State private var editingTrigger: Trigger?
    @State private var showAdd = false

    var body: some View {
        NavigationStack {
            List {
                if triggers.isEmpty {
                    Text("No triggers defined yet.")
                        .foregroundColor(.gray)
                } else {
                    ForEach(triggers) { trigger in
                        triggerRow(trigger)
                    }
                    .onDelete { indices in
                        for i in indices {
                            triggerRepo.remove(triggers[i].name)
                        }
                        refresh()
                    }
                }
            }
            .navigationTitle("Triggers")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Close") { dismiss() }
                }
                ToolbarItem(placement: .primaryAction) {
                    Button("Add") { showAdd = true }
                }
            }
            .sheet(isPresented: $showAdd) {
                EditTriggerView(title: "Add Trigger") { trigger in
                    triggerRepo.add(trigger)
                    refresh()
                }
            }
            .sheet(item: $editingTrigger) { trigger in
                EditTriggerView(title: "Edit Trigger", initial: trigger) { updated in
                    if updated.name != trigger.name { triggerRepo.remove(trigger.name) }
                    triggerRepo.add(updated)
                    refresh()
                }
            }
            .onAppear { refresh() }
        }
    }

    private func triggerRow(_ trigger: Trigger) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(trigger.name)
                    .fontWeight(.bold)
                    .foregroundColor(trigger.enabled ? .primary : .gray)
                HStack(spacing: 4) {
                    Text("/\(trigger.pattern)/")
                    if trigger.gag { Text("[gag]") }
                    if trigger.hilite { Text("[hilite]") }
                    if !trigger.body.isEmpty { Text("[cmd]") }
                    if trigger.shots >= 0 { Text("[\(trigger.shots) shots]") }
                }
                .font(.caption)
                .foregroundColor(.gray)
            }
            Spacer()
            Button(trigger.enabled ? "On" : "Off") {
                var t = trigger
                t.enabled.toggle()
                triggerRepo.add(t)
                refresh()
            }
            .font(.caption)
            .foregroundColor(trigger.enabled ? .green : .gray)
        }
        .swipeActions(edge: .trailing) {
            Button(role: .destructive) {
                triggerRepo.remove(trigger.name)
                refresh()
            } label: { Label("Delete", systemImage: "trash") }

            Button {
                editingTrigger = trigger
            } label: { Label("Edit", systemImage: "pencil") }
            .tint(.blue)
        }
    }

    private func refresh() {
        triggers = triggerRepo.load()
        onChanged()
    }
}

// MARK: - Edit Trigger

struct EditTriggerView: View {
    @Environment(\.dismiss) var dismiss
    let title: String
    var initial: Trigger?
    var onSave: (Trigger) -> Void

    @State private var name = ""
    @State private var pattern = ""
    @State private var body = ""
    @State private var priority = "0"
    @State private var shots = "-1"
    @State private var gag = false
    @State private var hilite = false
    @State private var patternError: String?

    var body: some View {
        NavigationStack {
            Form {
                Section("Identity") {
                    TextField("Name", text: $name)
                    TextField("Pattern (regex)", text: $pattern)
                        .font(.system(.body, design: .monospaced))
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                        .onChange(of: pattern) { _, newValue in
                            validatePattern(newValue)
                        }
                    if let err = patternError {
                        Text(err)
                            .font(.caption)
                            .foregroundColor(.red)
                    }
                }
                Section("Action") {
                    TextField("Command to send", text: $body)
                        .font(.system(.body, design: .monospaced))
                    HStack {
                        TextField("Priority", text: $priority)
                            .keyboardType(.numbersAndPunctuation)
                        TextField("Shots (-1=inf)", text: $shots)
                            .keyboardType(.numbersAndPunctuation)
                    }
                }
                Section("Flags") {
                    Toggle("Gag (suppress line)", isOn: $gag)
                    Toggle("Highlight match", isOn: $hilite)
                }
            }
            .navigationTitle(title)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        guard !name.isEmpty, !pattern.isEmpty, patternError == nil else { return }
                        let trigger = Trigger(
                            name: name.trimmingCharacters(in: .whitespaces),
                            pattern: pattern.trimmingCharacters(in: .whitespaces),
                            body: body.trimmingCharacters(in: .whitespaces),
                            priority: Int(priority) ?? 0,
                            shots: Int(shots) ?? -1,
                            gag: gag,
                            hilite: hilite
                        )
                        dismiss()
                        onSave(trigger)
                    }
                }
            }
            .onAppear {
                if let t = initial {
                    name = t.name; pattern = t.pattern; body = t.body
                    priority = String(t.priority); shots = String(t.shots)
                    gag = t.gag; hilite = t.hilite
                }
            }
        }
    }

    private func validatePattern(_ p: String) {
        if p.isEmpty { patternError = nil; return }
        do {
            _ = try Regex(p)
            patternError = nil
        } catch {
            patternError = error.localizedDescription
        }
    }
}
