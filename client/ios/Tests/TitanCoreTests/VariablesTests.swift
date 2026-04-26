import XCTest
@testable import TitanCore

final class VariablesTests: XCTestCase {

    func testWorldNamespaceResolves() {
        let store = VariableStore()
        XCTAssertEqual(store.resolve("world.name", worldName: "Hogwarts"), "Hogwarts")
        XCTAssertEqual(store.resolve("world.character", character: "Hermione"), "Hermione")
        XCTAssertEqual(store.resolve("world.host", host: "mud.example"), "mud.example")
        XCTAssertEqual(store.resolve("world.port", port: 4201), "4201")
        XCTAssertEqual(store.resolve("world.connected", connected: true), "1")
        XCTAssertEqual(store.resolve("world.connected", connected: false), "0")
        XCTAssertNil(store.resolve("world.bogus"))
    }

    func testEventNamespaceResolves() {
        let store = VariableStore()
        XCTAssertEqual(store.resolve("event.line", eventLine: "you say hello"), "you say hello")
        XCTAssertEqual(store.resolve("event.cause", eventCause: "trigger"), "trigger")
        XCTAssertNil(store.resolve("event.bogus"))
    }

    func testRegexpCaptureLookup() {
        let store = VariableStore()
        store.regexpCaptures = ["full match", "first", "second"]
        XCTAssertEqual(store.resolve("regexp.0"), "full match")
        XCTAssertEqual(store.resolve("regexp.1"), "first")
        XCTAssertEqual(store.resolve("regexp.2"), "second")
        XCTAssertNil(store.resolve("regexp.3"))   // out of range
        XCTAssertNil(store.resolve("regexp.bad")) // non-numeric
    }

    func testTempAndWorldTempLookup() {
        let store = VariableStore()
        store.temp["color"] = "blue"
        store.worldTemp["mode"] = "stealth"
        XCTAssertEqual(store.resolve("temp.color"), "blue")
        XCTAssertEqual(store.resolve("worldtemp.mode"), "stealth")
        XCTAssertNil(store.resolve("temp.missing"))
    }

    func testBareKeyFallsBackToTemp() {
        let store = VariableStore()
        store.temp["greeting"] = "hi"
        XCTAssertEqual(store.resolve("greeting"), "hi")
    }

    func testUnknownNamespaceReturnsNil() {
        let store = VariableStore()
        XCTAssertNil(store.resolve("nonsense.foo"))
    }

    func testExpandSubstitutesKnownVariables() {
        let store = VariableStore()
        let out = store.expand("at $world.name as $world.character",
                               worldName: "Mux", character: "Wizard")
        XCTAssertEqual(out, "at Mux as Wizard")
    }

    func testExpandLeavesUnknownReferenceLiteral() {
        let store = VariableStore()
        let out = store.expand("hello $world.unknown there", worldName: "X")
        // resolve returns nil → expand keeps the original $... text
        XCTAssertEqual(out, "hello $world.unknown there")
    }

    func testExpandHandlesSingleCharacterVariableName() {
        // Pattern requires at least 2 chars after $; this surfaces whether
        // single-letter variable references are reachable.
        let store = VariableStore()
        store.temp["x"] = "ONE"
        let out = store.expand("value=$x.")
        XCTAssertEqual(out, "value=ONE.", "single-letter $x should expand to its temp value")
    }
}
