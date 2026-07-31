import XCTest
@testable import TitanCore

final class McpParserTests: XCTestCase {

    func testNonMcpLineReturnsFalseAndIsNotConsumed() {
        let parser = McpParser()
        XCTAssertFalse(parser.processLine("hello world"))
        XCTAssertFalse(parser.processLine(""))
        XCTAssertFalse(parser.processLine("#$ not quite"))
    }

    func testMcpPrefixedLineReturnsTrue() {
        let parser = McpParser()
        XCTAssertTrue(parser.processLine("#$#mcp authentication-key: x version: 2.1 to: 2.1"))
    }

    func testMcpInitTriggersAuthAndNegotiation() {
        let parser = McpParser()
        var sent: [String] = []
        parser.sendRaw = { sent.append($0) }

        _ = parser.processLine("#$#mcp authentication-key: SERVER-KEY version: 2.1 to: 2.1")

        XCTAssertNotNil(parser.sessionKey)
        let key = parser.sessionKey!
        XCTAssertEqual(key.count, 16)

        XCTAssertTrue(sent.contains { $0.hasPrefix("#$#mcp authentication-key: \(key) version:") })
        XCTAssertTrue(sent.contains { $0.contains("mcp-negotiate-can \(key) package: dns-org-mud-moo-simpleedit") })
        XCTAssertTrue(sent.contains { $0 == "#$#mcp-negotiate-end \(key)" })
    }

    func testIncompatibleVersionsLeaveSessionUnestablished() {
        let parser = McpParser()
        var sent: [String] = []
        parser.sendRaw = { sent.append($0) }

        // Server min 3.0 .. max 3.0, client is fixed at 2.1 — no overlap.
        _ = parser.processLine("#$#mcp authentication-key: x version: 3.0 to: 3.0")

        XCTAssertNil(parser.sessionKey)
        XCTAssertEqual(sent, [])
    }

    func testNegotiateCanRecordsServerPackage() {
        let parser = McpParser()
        parser.sendRaw = { _ in }
        _ = parser.processLine("#$#mcp authentication-key: x version: 2.1 to: 2.1")
        let key = parser.sessionKey!

        _ = parser.processLine("#$#mcp-negotiate-can \(key) package: dns-org-mud-moo-simpleedit min-version: 1.0 max-version: 1.0")

        let recorded = parser.serverPackages["dns-org-mud-moo-simpleedit"]
        XCTAssertEqual(recorded?.0, 1.0)
        XCTAssertEqual(recorded?.1, 1.0)
    }

    func testNegotiateEndSetsNegotiatedFlag() {
        let parser = McpParser()
        parser.sendRaw = { _ in }
        _ = parser.processLine("#$#mcp authentication-key: x version: 2.1 to: 2.1")
        let key = parser.sessionKey!

        XCTAssertFalse(parser.negotiated)
        _ = parser.processLine("#$#mcp-negotiate-end \(key)")
        XCTAssertTrue(parser.negotiated)
    }

    func testWrongAuthKeyIsRejected() {
        let parser = McpParser()
        parser.sendRaw = { _ in }
        _ = parser.processLine("#$#mcp authentication-key: x version: 2.1 to: 2.1")
        XCTAssertNotNil(parser.sessionKey)

        // Wrong auth key — should NOT set negotiated.
        _ = parser.processLine("#$#mcp-negotiate-end WRONG-KEY")
        XCTAssertFalse(parser.negotiated)
    }

    func testSimpleEditMultilineAssembly() {
        let parser = McpParser()
        parser.sendRaw = { _ in }
        _ = parser.processLine("#$#mcp authentication-key: x version: 2.1 to: 2.1")
        let key = parser.sessionKey!

        var captured: (String, String, String, String)?
        parser.onEditRequest = { ref, name, type, content in
            captured = (ref, name, type, content)
        }

        _ = parser.processLine("#$#dns-org-mud-moo-simpleedit-content \(key) reference: #123/desc name: \"room desc\" type: string-list content*: \"\" _data-tag: T1")
        _ = parser.processLine("#$#* T1 content: line one")
        _ = parser.processLine("#$#* T1 content: line two")
        _ = parser.processLine("#$#: T1")

        XCTAssertNotNil(captured)
        XCTAssertEqual(captured?.0, "#123/desc")
        XCTAssertEqual(captured?.1, "room desc")
        XCTAssertEqual(captured?.2, "string-list")
        XCTAssertEqual(captured?.3, "line one\nline two")
    }

    func testSendSimpleEditSetEmitsHeaderContinuationsAndEnd() {
        let parser = McpParser()
        parser.sendRaw = { _ in }
        _ = parser.processLine("#$#mcp authentication-key: x version: 2.1 to: 2.1")
        let key = parser.sessionKey!

        var sent: [String] = []
        parser.sendRaw = { sent.append($0) }

        parser.sendSimpleEditSet(reference: "#5/desc", type: "string-list", content: "alpha\nbeta")

        XCTAssertEqual(sent.count, 4)
        XCTAssertTrue(sent[0].hasPrefix("#$#dns-org-mud-moo-simpleedit-set \(key) reference: #5/desc"))
        XCTAssertTrue(sent[0].contains("_data-tag: "))
        XCTAssertTrue(sent[1].hasSuffix("content: alpha"))
        XCTAssertTrue(sent[2].hasSuffix("content: beta"))
        XCTAssertTrue(sent[3].hasPrefix("#$#: "))
    }

    // #1893: multiline reassembly is bounded (mirrors web #1889).

    func testPendingMultilineCountIsCapped() {
        let parser = McpParser()
        parser.sessionKey = "key"
        var diags: [String] = []
        parser.onDiagnostic = { diags.append($0) }

        for i in 0..<40 {
            _ = parser.processLine(
                "#$#dns-org-mud-moo-simpleedit-content key reference: r content*: \"\" _data-tag: t\(i)")
        }
        XCTAssertLessThanOrEqual(parser.pendingCount, MCP_MAX_PENDING_MESSAGES)
        XCTAssertTrue(diags.contains { $0.range(of: "evict|capacity|pending", options: .regularExpression) != nil })
    }

    func testOversizedContinuationDropsTag() {
        let parser = McpParser()
        parser.sessionKey = "key"
        var diags: [String] = []
        parser.onDiagnostic = { diags.append($0) }

        _ = parser.processLine(
            "#$#dns-org-mud-moo-simpleedit-content key reference: r content*: \"\" _data-tag: fat")
        XCTAssertTrue(parser.isPending("fat"))

        let chunk = String(repeating: "x", count: 1024)
        for _ in 0..<300 {
            _ = parser.processLine("#$#* fat content: \(chunk)")
            if !parser.isPending("fat") { break }
        }
        XCTAssertFalse(parser.isPending("fat"))
        XCTAssertTrue(diags.contains { $0.range(of: "size limit|dropped", options: .regularExpression) != nil })
    }

    func testCompleteMultilineStillDispatches() {
        let parser = McpParser()
        parser.sessionKey = "key"
        var edit: (String, String, String, String)?
        parser.onEditRequest = { ref, name, type, content in
            edit = (ref, name, type, content)
        }

        _ = parser.processLine(
            "#$#dns-org-mud-moo-simpleedit-content key reference: R name: N type: string-list content*: \"\" _data-tag: ok")
        _ = parser.processLine("#$#* ok content: line1")
        _ = parser.processLine("#$#* ok content: line2")
        _ = parser.processLine("#$#: ok")

        XCTAssertNotNil(edit)
        XCTAssertEqual(edit?.3, "line1\nline2")
        XCTAssertEqual(parser.pendingCount, 0)
    }
}
