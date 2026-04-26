import XCTest
@testable import TitanCore

final class TriggerEngineTests: XCTestCase {

    private func trigger(_ name: String, pattern: String,
                         body: String = "", priority: Int = 0,
                         shots: Int = -1, gag: Bool = false,
                         hilite: Bool = false, enabled: Bool = true,
                         substituteFind: String = "",
                         substituteReplace: String = "",
                         lineClass: String = "",
                         conditions: [TriggerCondition] = [],
                         conditionsAnded: Bool = true) -> Trigger {
        Trigger(name: name, pattern: pattern, body: body, priority: priority,
                shots: shots, gag: gag, hilite: hilite, enabled: enabled,
                substituteFind: substituteFind, substituteReplace: substituteReplace,
                lineClass: lineClass, conditions: conditions,
                conditionsAnded: conditionsAnded)
    }

    func testSimpleRegexMatchSetsMatched() {
        let engine = TriggerEngine()
        engine.load([trigger("greet", pattern: "hello")])
        let r = engine.check("the wizard says hello")
        XCTAssertTrue(r.matched)
        XCTAssertFalse(r.gagged)
    }

    func testNonMatchingLineYieldsNoMatch() {
        let engine = TriggerEngine()
        engine.load([trigger("greet", pattern: "hello")])
        XCTAssertFalse(engine.check("nothing here").matched)
    }

    func testGagFlagPropagatesToResult() {
        let engine = TriggerEngine()
        engine.load([trigger("spam", pattern: "buy now", gag: true)])
        let r = engine.check("buy now cheap")
        XCTAssertTrue(r.gagged)
    }

    func testHiliteWrapsMatchInAnsiBold() {
        let engine = TriggerEngine()
        engine.load([trigger("name", pattern: "Frodo", hilite: true)])
        let r = engine.check("Frodo enters")
        XCTAssertEqual(r.displayLine, "\u{1b}[1mFrodo\u{1b}[22m enters")
    }

    func testSubstituteRewritesDisplayText() {
        let engine = TriggerEngine()
        engine.load([trigger("redact", pattern: "secret",
                             substituteFind: "secret", substituteReplace: "[REDACTED]")])
        let r = engine.check("the secret is out")
        XCTAssertEqual(r.displayLine, "the [REDACTED] is out")
    }

    func testBodyExpansionUsesDollarZero() {
        let engine = TriggerEngine()
        engine.load([trigger("echo", pattern: "ping (\\w+)", body: "say got $0")])
        let r = engine.check("ping alpha")
        XCTAssertEqual(r.commands, ["say got ping alpha"])
    }

    func testDisabledTriggerDoesNotMatch() {
        let engine = TriggerEngine()
        engine.load([trigger("off", pattern: "hello", enabled: false)])
        XCTAssertFalse(engine.check("hello there").matched)
    }

    func testZeroShotsTriggerIsSkipped() {
        let engine = TriggerEngine()
        engine.load([trigger("expired", pattern: "hello", shots: 0)])
        XCTAssertFalse(engine.check("hello there").matched)
    }

    func testLineClassRecorded() {
        let engine = TriggerEngine()
        engine.load([trigger("classify", pattern: "tells you", lineClass: "tell")])
        let r = engine.check("Frodo tells you, hi")
        XCTAssertTrue(r.lineClasses.contains("tell"))
    }

    func testCompositeAndedRequiresAllConditions() {
        let engine = TriggerEngine()
        engine.load([trigger("guard", pattern: "ready",
                             conditions: [.worldConnected(),
                                          .stringMatch(pattern: "wizard")],
                             conditionsAnded: true)])

        var ctx = ConditionContext()
        ctx.isConnected = true
        XCTAssertTrue(engine.check("the wizard is ready", context: ctx).matched)

        // wizard match present but not connected → fails
        ctx.isConnected = false
        XCTAssertFalse(engine.check("the wizard is ready", context: ctx).matched)

        // connected but no "wizard" in line → fails
        ctx.isConnected = true
        XCTAssertFalse(engine.check("the rogue is ready", context: ctx).matched)
    }

    func testCompositeOredAcceptsAnyCondition() {
        let engine = TriggerEngine()
        engine.load([trigger("anyOf", pattern: "ping",
                             conditions: [.worldConnected(),
                                          .lineClass(className: "vip")],
                             conditionsAnded: false)])

        var ctx = ConditionContext()
        ctx.isConnected = true
        XCTAssertTrue(engine.check("ping", context: ctx).matched)

        ctx.isConnected = false
        ctx.lineClasses = ["vip"]
        XCTAssertTrue(engine.check("ping", context: ctx).matched)

        ctx.lineClasses = []
        XCTAssertFalse(engine.check("ping", context: ctx).matched)
    }
}
