import Foundation

// MARK: - Session Logger

class SessionLogger {
    private var fileHandle: FileHandle?
    private var logFile: URL?
    private(set) var active = false

    var logDir: URL {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first!
        let dir = docs.appendingPathComponent("logs", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    func start(worldName: String, filename: String? = nil) -> URL {
        stop()
        let safe = worldName.replacingOccurrences(of: "[^a-zA-Z0-9._-]", with: "_",
                                                   options: .regularExpression)
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyyMMdd_HHmmss"
        let ts = formatter.string(from: Date())
        let name = filename ?? "\(safe)_\(ts).log"
        let file = logDir.appendingPathComponent(name)
        FileManager.default.createFile(atPath: file.path, contents: nil)
        fileHandle = try? FileHandle(forWritingTo: file)
        fileHandle?.seekToEndOfFile()
        logFile = file
        active = true
        writeLine("--- Log started: \(Date()) ---")
        return file
    }

    func stop() {
        guard active else { return }
        writeLine("--- Log ended: \(Date()) ---")
        try? fileHandle?.close()
        fileHandle = nil
        logFile = nil
        active = false
    }

    func writeLine(_ line: String) {
        guard active, let data = "\(line)\n".data(using: .utf8) else { return }
        fileHandle?.write(data)
    }

    func currentFile() -> URL? { logFile }
}
