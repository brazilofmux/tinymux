import Foundation

// #1893 / #1889: bound multiline reassembly so a hostile game cannot grow
// the client heap without bound via unterminated #$#* / unique _data-tag floods.
// Limits match the web client (client/web/js/mcp.js).
let MCP_MAX_PENDING_MESSAGES = 32
let MCP_MAX_MESSAGE_BYTES = 256 * 1024       // per reassembled message
let MCP_MAX_TOTAL_PENDING_BYTES = 1024 * 1024

// MARK: - MCP Message

class McpMessage {
    let messageName: String
    let authKey: String
    var attributes: [String: String] = [:]
    var multilineKeys: Set<String> = []
    var dataTag: String?
    var finished = false
    /// Approximate attribute payload size for pending caps.
    var byteSize = 0

    init(messageName: String, authKey: String) {
        self.messageName = messageName
        self.authKey = authKey
    }

    func addLine(key: String, value: String) {
        if let existing = attributes[key] {
            attributes[key] = "\(existing)\n\(value)"
        } else {
            attributes[key] = value
        }
    }
}

// MARK: - MCP Edit State

struct McpEditState: Identifiable {
    let id = UUID()
    let reference: String
    let name: String
    let type: String
    let content: String
    let tabIndex: Int
}

// MARK: - MCP Parser

class McpParser {
    var sessionKey: String?
    var negotiated = false

    private let supportedPackages: [String: (Double, Double)] = [
        "dns-org-mud-moo-simpleedit": (1.0, 1.0),
    ]

    // Dictionary is unordered; pendingOrder tracks insertion for oldest-first eviction.
    private var pending: [String: McpMessage] = [:]
    private var pendingOrder: [String] = []
    private var pendingBytes = 0

    var serverPackages: [String: (Double, Double)] = [:]

    var onEditRequest: ((String, String, String, String) -> Void)?
    var sendRaw: ((String) -> Void)?
    /// Optional diagnostic sink for capacity / drop events (#1893).
    var onDiagnostic: ((String) -> Void)?

    /// Number of unfinished multiline messages — exposed for tests.
    var pendingCount: Int { pending.count }

    func isPending(_ tag: String) -> Bool { pending[tag] != nil }

    // MARK: - Process line; returns true if MCP (should be hidden)

    func processLine(_ line: String) -> Bool {
        guard line.hasPrefix("#$#") else { return false }

        if line.hasPrefix("#$#*") {
            handleContinuation(line)
        } else if line.hasPrefix("#$#:") {
            handleMultilineEnd(line)
        } else {
            handleMessage(line)
        }
        return true
    }

    // MARK: - Capacity helpers

    private func diag(_ msg: String) {
        onDiagnostic?(msg)
    }

    private func attrBytes(_ msg: McpMessage) -> Int {
        msg.attributes.reduce(0) { $0 + $1.key.count + $1.value.count }
    }

    private func dropPending(_ tag: String, reason: String) {
        guard let msg = pending.removeValue(forKey: tag) else { return }
        pendingOrder.removeAll { $0 == tag }
        pendingBytes -= msg.byteSize
        if pendingBytes < 0 { pendingBytes = 0 }
        diag("MCP: dropped pending multiline tag=\(tag) (\(reason))")
    }

    /// Evict oldest pending entries until under caps.
    private func evictForRoom(needBytes: Int) {
        while !pendingOrder.isEmpty
            && (pending.count >= MCP_MAX_PENDING_MESSAGES
                || pendingBytes + needBytes > MCP_MAX_TOTAL_PENDING_BYTES)
        {
            // dropPending owns pendingOrder; do not removeFirst here.
            dropPending(pendingOrder[0], reason: "evicted for capacity")
        }
    }

    // MARK: - Parse regular message

    private func handleMessage(_ line: String) {
        let body = String(line.dropFirst(3)) // remove #$#
        let tokens = tokenize(body)
        guard tokens.count >= 2 else { return }

        let messageName = tokens[0]
        let authKey = tokens[1]

        if messageName != "mcp" && sessionKey != nil && authKey != sessionKey { return }

        let msg = McpMessage(messageName: messageName, authKey: authKey)

        var i = 2
        while i < tokens.count {
            let token = tokens[i]
            if token.hasSuffix(":") {
                let key = String(token.dropLast())
                let value = i + 1 < tokens.count ? tokens[i + 1] : ""
                if key == "_data-tag" {
                    msg.dataTag = value
                } else if key.hasSuffix("*") {
                    msg.multilineKeys.insert(String(key.dropLast()))
                } else {
                    msg.attributes[key] = value
                }
                i += 2
            } else {
                i += 1
            }
        }

        if let tag = msg.dataTag, !msg.multilineKeys.isEmpty {
            if pending[tag] != nil {
                dropPending(tag, reason: "replaced")
            }
            msg.byteSize = attrBytes(msg)
            if msg.byteSize > MCP_MAX_MESSAGE_BYTES {
                diag("MCP: multiline start exceeds per-message limit, ignored tag=\(tag)")
                return
            }
            evictForRoom(needBytes: msg.byteSize)
            if pending.count >= MCP_MAX_PENDING_MESSAGES
                || pendingBytes + msg.byteSize > MCP_MAX_TOTAL_PENDING_BYTES
            {
                diag("MCP: pending capacity full, ignored tag=\(tag)")
                return
            }
            pending[tag] = msg
            pendingOrder.append(tag)
            pendingBytes += msg.byteSize
        } else {
            msg.finished = true
            dispatch(msg)
        }
    }

    private func handleContinuation(_ line: String) {
        let body = String(line.dropFirst(5)) // remove "#$#* "
        guard let spaceIdx = body.firstIndex(of: " ") else { return }
        let tag = String(body[body.startIndex..<spaceIdx])
        let rest = String(body[body.index(after: spaceIdx)...])

        guard let msg = pending[tag] else { return }
        guard let colonIdx = rest.range(of: ": ") else { return }
        let key = String(rest[rest.startIndex..<colonIdx.lowerBound])
        let value = String(rest[colonIdx.upperBound...])
        let prev = msg.attributes[key] ?? ""
        let next = prev.isEmpty ? value : "\(prev)\n\(value)"
        let delta = next.count - prev.count
        if msg.byteSize + delta > MCP_MAX_MESSAGE_BYTES {
            dropPending(tag, reason: "per-message size limit")
            return
        }
        if pendingBytes + delta > MCP_MAX_TOTAL_PENDING_BYTES {
            dropPending(tag, reason: "total pending size limit")
            return
        }
        msg.attributes[key] = next
        msg.byteSize += delta
        pendingBytes += delta
    }

    private func handleMultilineEnd(_ line: String) {
        let tag = String(line.dropFirst(5)).trimmingCharacters(in: .whitespaces)
        guard let msg = pending.removeValue(forKey: tag) else { return }
        pendingOrder.removeAll { $0 == tag }
        pendingBytes -= msg.byteSize
        if pendingBytes < 0 { pendingBytes = 0 }
        msg.finished = true
        dispatch(msg)
    }

    // MARK: - Dispatch

    private func dispatch(_ msg: McpMessage) {
        switch msg.messageName {
        case "mcp":
            handleMcpInit(msg)
        case "mcp-negotiate-can":
            handleNegotiateCan(msg)
        case "mcp-negotiate-end":
            negotiated = true
        case "dns-org-mud-moo-simpleedit-content":
            handleSimpleEditContent(msg)
        default:
            break
        }
    }

    private func handleMcpInit(_ msg: McpMessage) {
        guard let serverMin = msg.attributes["version"].flatMap(Double.init),
              let serverMax = msg.attributes["to"].flatMap(Double.init) else { return }

        let clientMin = 2.1, clientMax = 2.1
        guard clientMax >= serverMin && serverMax >= clientMin else { return }

        sessionKey = generateKey()
        guard let key = sessionKey else { return }

        sendRaw?("#$#mcp authentication-key: \(key) version: \(clientMin) to: \(clientMax)")
        for (pkg, versions) in supportedPackages {
            sendRaw?("#$#mcp-negotiate-can \(key) package: \(pkg) min-version: \(versions.0) max-version: \(versions.1)")
        }
        sendRaw?("#$#mcp-negotiate-end \(key)")
    }

    private func handleNegotiateCan(_ msg: McpMessage) {
        guard let pkg = msg.attributes["package"],
              let min = msg.attributes["min-version"].flatMap(Double.init),
              let max = msg.attributes["max-version"].flatMap(Double.init) else { return }
        serverPackages[pkg] = (min, max)
    }

    private func handleSimpleEditContent(_ msg: McpMessage) {
        let reference = msg.attributes["reference"] ?? ""
        let name = msg.attributes["name"] ?? reference
        let type = msg.attributes["type"] ?? "string-list"
        let content = msg.attributes["content"] ?? ""
        onEditRequest?(reference, name, type, content)
    }

    // MARK: - Send edited text back

    func sendSimpleEditSet(reference: String, type: String, content: String) {
        guard let key = sessionKey else { return }
        let tag = generateTag()
        sendRaw?("#$#dns-org-mud-moo-simpleedit-set \(key) reference: \(reference) type: \(type) content*: \"\" _data-tag: \(tag)")
        for line in content.components(separatedBy: "\n") {
            sendRaw?("#$#* \(tag) content: \(line)")
        }
        sendRaw?("#$#: \(tag)")
    }

    // MARK: - Helpers

    private var tagCounter = 0

    private func generateKey() -> String {
        let chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        return String((0..<16).map { _ in chars.randomElement()! })
    }

    private func generateTag() -> String {
        tagCounter += 1
        return "T\(tagCounter)"
    }

    private func tokenize(_ input: String) -> [String] {
        var tokens: [String] = []
        var i = input.startIndex
        while i < input.endIndex {
            while i < input.endIndex && input[i] == " " { i = input.index(after: i) }
            guard i < input.endIndex else { break }

            if input[i] == "\"" {
                i = input.index(after: i)
                var s = ""
                while i < input.endIndex && input[i] != "\"" {
                    if input[i] == "\\" && input.index(after: i) < input.endIndex {
                        i = input.index(after: i)
                        s.append(input[i])
                    } else {
                        s.append(input[i])
                    }
                    i = input.index(after: i)
                }
                if i < input.endIndex { i = input.index(after: i) }
                tokens.append(s)
            } else {
                let start = i
                while i < input.endIndex && input[i] != " " { i = input.index(after: i) }
                tokens.append(String(input[start..<i]))
            }
        }
        return tokens
    }
}
