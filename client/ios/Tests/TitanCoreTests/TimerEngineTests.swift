import XCTest
@testable import TitanCore

@MainActor
final class TimerEngineTests: XCTestCase {

    func testAddRecordsTimer() {
        let engine = TimerEngine()
        engine.add(name: "t1", command: "look", intervalSeconds: 60)
        let names = engine.list().map(\.name)
        XCTAssertEqual(names, ["t1"])
    }

    func testRemoveDeletesTimer() {
        let engine = TimerEngine()
        engine.add(name: "t1", command: "look", intervalSeconds: 60)
        XCTAssertTrue(engine.remove("t1"))
        XCTAssertTrue(engine.list().isEmpty)
    }

    func testRemoveReturnsFalseForUnknown() {
        let engine = TimerEngine()
        XCTAssertFalse(engine.remove("ghost"))
    }

    func testReAddReplacesPriorTimer() {
        let engine = TimerEngine()
        engine.add(name: "t1", command: "look", intervalSeconds: 60)
        engine.add(name: "t1", command: "score", intervalSeconds: 30)
        let timers = engine.list()
        XCTAssertEqual(timers.count, 1)
        XCTAssertEqual(timers.first?.command, "score")
        XCTAssertEqual(timers.first?.intervalSeconds, 30)
    }

    func testCancelAllEmptiesList() {
        let engine = TimerEngine()
        engine.add(name: "t1", command: "look", intervalSeconds: 60)
        engine.add(name: "t2", command: "score", intervalSeconds: 30)
        engine.cancelAll()
        XCTAssertTrue(engine.list().isEmpty)
    }

    func testCallbackFiresAfterInterval() async {
        let engine = TimerEngine()
        var fires: [(String, String)] = []
        engine.onFire = { fires.append(($0, $1)) }

        engine.add(name: "t1", command: "look", intervalSeconds: 0.05, shots: 1)
        try? await Task.sleep(for: .milliseconds(150))

        XCTAssertEqual(fires.map(\.0), ["t1"])
        XCTAssertEqual(fires.map(\.1), ["look"])
        XCTAssertTrue(engine.list().isEmpty, "single-shot timer should self-remove")
    }

    func testListReflectsDecrementedShotsRemaining() async {
        // MudTimer is a struct: closure holds its own copy, dict holds another.
        // If the closure decrements only its local copy, list() will be stale.
        let engine = TimerEngine()
        engine.onFire = { _, _ in }

        engine.add(name: "t1", command: "look", intervalSeconds: 0.05, shots: 3)
        try? await Task.sleep(for: .milliseconds(80))

        guard let timer = engine.list().first(where: { $0.name == "t1" }) else {
            XCTFail("timer should still exist after one fire (3 → 2)")
            return
        }
        XCTAssertEqual(timer.shotsRemaining, 2,
                       "list() should report 2 shots remaining after first fire of a 3-shot timer")
        engine.remove("t1")
    }
}
