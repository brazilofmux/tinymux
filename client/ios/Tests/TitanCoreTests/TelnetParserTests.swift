import XCTest
@testable import TitanCore

final class TelnetParserTests: XCTestCase {

    func testPlainTextEmitsAsLine() {
        var parser = TelnetParser()
        var lines: [String] = []
        parser.onLine = { lines.append($0) }

        parser.process(Data("hello\r\n".utf8))

        XCTAssertEqual(lines, ["hello"])
    }

    func testEscapedIACDeliversLiteral0xFF() {
        var parser = TelnetParser()
        var captured = Data()
        parser.onText = { captured.append($0) }

        // IAC IAC -> single 0xFF byte in output
        parser.process(Data([0xFF, 0xFF]))

        XCTAssertEqual(captured, Data([0xFF]))
    }

    func testWillEchoTriggersDoEchoAndSetsRemoteEcho() {
        var parser = TelnetParser()
        var sent: [Data] = []
        parser.sendRaw = { sent.append($0) }

        // IAC WILL ECHO (251, 1)
        parser.process(Data([0xFF, 251, 1]))

        XCTAssertTrue(parser.remoteEcho)
        XCTAssertEqual(sent, [Data([0xFF, 253, 1])]) // IAC DO ECHO
    }

    func testGoAheadFlushesPendingTextAsPrompt() {
        var parser = TelnetParser()
        var prompts: [String] = []
        parser.onPrompt = { prompts.append($0) }

        // "ready> " then IAC GA (255, 249)
        var data = Data("ready> ".utf8)
        data.append(contentsOf: [0xFF, 249])
        parser.process(data)

        XCTAssertEqual(prompts, ["ready> "])
    }

    func testDoNawsRespondsWithWillThenSubnegotiation() {
        var parser = TelnetParser()
        parser.nawsWidth = 100
        parser.nawsHeight = 30
        var sent: [Data] = []
        parser.sendRaw = { sent.append($0) }

        // IAC DO NAWS (255, 253, 31)
        parser.process(Data([0xFF, 253, 31]))

        XCTAssertEqual(sent.count, 2)
        XCTAssertEqual(sent[0], Data([0xFF, 251, 31])) // IAC WILL NAWS
        // IAC SB NAWS 0 100 0 30 IAC SE
        XCTAssertEqual(sent[1], Data([0xFF, 250, 31, 0, 100, 0, 30, 0xFF, 240]))
    }

    // #1788: oversized IAC SB must not grow without bound; stay in SB and
    // discard on SE rather than treating payload as typed text.
    func testOversizedSubnegotiationIsCappedAndDiscarded() {
        var parser = TelnetParser()
        var lines: [String] = []
        parser.onLine = { lines.append($0) }

        // IAC SB GMCP (201) + 5000 payload bytes + IAC SE, then "ok\n"
        var data = Data([0xFF, 250, 201])
        data.append(Data(repeating: 0x41, count: 5000))
        data.append(contentsOf: [0xFF, 240])
        data.append(contentsOf: "ok\n".utf8)
        parser.process(data)

        XCTAssertEqual(lines, ["ok"])
    }
}
