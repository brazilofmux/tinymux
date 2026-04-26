import Foundation

// MARK: - Timer Engine

struct MudTimer {
    let name: String
    let command: String
    let intervalSeconds: Double
    var shotsRemaining: Int
}

@MainActor
class TimerEngine {
    private var timers: [String: (MudTimer, Task<Void, Never>)] = [:]
    var onFire: ((String, String) -> Void)?

    func add(name: String, command: String, intervalSeconds: Double, shots: Int = -1) {
        remove(name)
        let initial = MudTimer(name: name, command: command,
                               intervalSeconds: intervalSeconds, shotsRemaining: shots)
        let task = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(intervalSeconds))
                guard !Task.isCancelled else { break }
                guard let self else { break }
                self.onFire?(name, command)
                // The dict is the source of truth: mutate the stored copy so
                // list() reflects the decremented shotsRemaining.
                guard var entry = self.timers[name] else { break }
                if entry.0.shotsRemaining > 0 {
                    entry.0.shotsRemaining -= 1
                    if entry.0.shotsRemaining == 0 {
                        self.timers.removeValue(forKey: name)
                        break
                    } else {
                        self.timers[name] = entry
                    }
                }
            }
        }
        timers[name] = (initial, task)
    }

    @discardableResult
    func remove(_ name: String) -> Bool {
        guard let entry = timers.removeValue(forKey: name) else { return false }
        entry.1.cancel()
        return true
    }

    func list() -> [MudTimer] {
        timers.values.map(\.0)
    }

    func cancelAll() {
        timers.values.forEach { $0.1.cancel() }
        timers.removeAll()
    }
}
