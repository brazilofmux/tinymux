package org.tinymux.titan.data

// MARK: - MCP Message

data class McpMessage(
    val messageName: String,
    val authKey: String,
    val attributes: MutableMap<String, String> = mutableMapOf(),
    val multilineKeys: MutableSet<String> = mutableSetOf(),
    var dataTag: String? = null,
    var finished: Boolean = false,
) {
    fun addLine(key: String, value: String) {
        val existing = attributes[key]
        attributes[key] = if (existing != null) "$existing\n$value" else value
    }
}

// MARK: - MCP Parser

class McpParser {
    var sessionKey: String? = null
    var negotiated = false

    // Packages we support (name → version range)
    private val supportedPackages = mapOf(
        "dns-org-mud-moo-simpleedit" to (1.0 to 1.0),
    )

    // Pending multiline messages by data tag
    private val pending = mutableMapOf<String, McpMessage>()

    // Server's advertised packages
    val serverPackages = mutableMapOf<String, Pair<Double, Double>>()

    // Callbacks
    var onEditRequest: ((reference: String, name: String, type: String, content: String) -> Unit)? = null
    var sendRaw: ((String) -> Unit)? = null

    // MARK: - Process a line; returns true if it was an MCP line (should be hidden)

    fun processLine(line: String): Boolean {
        if (line.startsWith("#\$#")) {
            // MCP out-of-band line
            if (line.startsWith("#\$#*")) {
                // Multiline continuation: #$#* <tag> <key>: <value>
                handleContinuation(line)
            } else if (line.startsWith("#\$#:")) {
                // Multiline end: #$#: <tag>
                handleMultilineEnd(line)
            } else {
                // Regular MCP message: #$#<name> <authkey> <key>: <value> ...
                handleMessage(line)
            }
            return true
        }
        return false
    }

    // MARK: - Parse regular message

    private fun handleMessage(line: String) {
        val body = line.removePrefix("#\$#")
        val tokens = tokenize(body)
        if (tokens.size < 2) return

        val messageName = tokens[0]
        val authKey = tokens[1]

        // Initial mcp handshake from server has no auth key to validate
        if (messageName != "mcp" && sessionKey != null && authKey != sessionKey) return

        val msg = McpMessage(messageName = messageName, authKey = authKey)

        // Parse key-value pairs
        var i = 2
        while (i < tokens.size) {
            val token = tokens[i]
            if (token.endsWith(":")) {
                val key = token.dropLast(1)
                val value = if (i + 1 < tokens.size) tokens[i + 1] else ""
                if (key == "_data-tag") {
                    msg.dataTag = value
                } else if (key.endsWith("*")) {
                    // Multiline key marker
                    msg.multilineKeys.add(key.dropLast(1))
                } else {
                    msg.attributes[key] = value
                }
                i += 2
            } else {
                i++
            }
        }

        if (msg.dataTag != null && msg.multilineKeys.isNotEmpty()) {
            // Multiline message — store as pending
            pending[msg.dataTag!!] = msg
        } else {
            // Single-line message — process immediately
            msg.finished = true
            dispatch(msg)
        }
    }

    // MARK: - Multiline continuation

    private fun handleContinuation(line: String) {
        // #$#* <tag> <key>: <value>
        val body = line.removePrefix("#\$#* ")
        val spaceIdx = body.indexOf(' ')
        if (spaceIdx < 0) return
        val tag = body.substring(0, spaceIdx)
        val rest = body.substring(spaceIdx + 1)

        val msg = pending[tag] ?: return
        val colonIdx = rest.indexOf(": ")
        if (colonIdx < 0) return
        val key = rest.substring(0, colonIdx)
        val value = rest.substring(colonIdx + 2)
        msg.addLine(key, value)
    }

    // MARK: - Multiline end

    private fun handleMultilineEnd(line: String) {
        val tag = line.removePrefix("#\$#: ").trim()
        val msg = pending.remove(tag) ?: return
        msg.finished = true
        dispatch(msg)
    }

    // MARK: - Dispatch to handlers

    private fun dispatch(msg: McpMessage) {
        when (msg.messageName) {
            "mcp" -> handleMcpInit(msg)
            "mcp-negotiate-can" -> handleNegotiateCan(msg)
            "mcp-negotiate-end" -> handleNegotiateEnd()
            "dns-org-mud-moo-simpleedit-content" -> handleSimpleEditContent(msg)
            else -> {} // Unknown package — ignore
        }
    }

    // MARK: - MCP Init (server hello)

    private fun handleMcpInit(msg: McpMessage) {
        val serverMin = msg.attributes["version"]?.toDoubleOrNull() ?: return
        val serverMax = msg.attributes["to"]?.toDoubleOrNull() ?: return

        // We support MCP 2.1
        val clientMin = 2.1
        val clientMax = 2.1

        if (clientMax >= serverMin && serverMax >= clientMin) {
            // Generate session key
            sessionKey = generateKey()

            // Send our mcp response
            sendRaw?.invoke("#\$#mcp authentication-key: $sessionKey version: $clientMin to: $clientMax")

            // Advertise our packages
            for ((pkg, versions) in supportedPackages) {
                sendRaw?.invoke("#\$#mcp-negotiate-can $sessionKey package: $pkg min-version: ${versions.first} max-version: ${versions.second}")
            }
            sendRaw?.invoke("#\$#mcp-negotiate-end $sessionKey")
        }
    }

    // MARK: - Negotiate Can (server advertises a package)

    private fun handleNegotiateCan(msg: McpMessage) {
        val pkg = msg.attributes["package"] ?: return
        val min = msg.attributes["min-version"]?.toDoubleOrNull() ?: return
        val max = msg.attributes["max-version"]?.toDoubleOrNull() ?: return
        serverPackages[pkg] = min to max
    }

    // MARK: - Negotiate End

    private fun handleNegotiateEnd() {
        negotiated = true
    }

    // MARK: - SimpleEdit Content (server sends text to edit)

    private fun handleSimpleEditContent(msg: McpMessage) {
        val reference = msg.attributes["reference"] ?: return
        val name = msg.attributes["name"] ?: reference
        val type = msg.attributes["type"] ?: "string-list"
        val content = msg.attributes["content"] ?: ""
        onEditRequest?.invoke(reference, name, type, content)
    }

    // MARK: - Send edited text back

    fun sendSimpleEditSet(reference: String, type: String, content: String) {
        val key = sessionKey ?: return
        val tag = generateTag()
        sendRaw?.invoke("#\$#dns-org-mud-moo-simpleedit-set $key reference: $reference type: $type content*: \"\" _data-tag: $tag")
        for (line in content.lines()) {
            sendRaw?.invoke("#\$#* $tag content: $line")
        }
        sendRaw?.invoke("#\$#: $tag")
    }

    // MARK: - Helpers

    private var tagCounter = 0

    private fun generateKey(): String {
        val chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        return (1..16).map { chars.random() }.joinToString("")
    }

    private fun generateTag(): String {
        tagCounter++
        return "T$tagCounter"
    }

    private fun tokenize(input: String): List<String> {
        val tokens = mutableListOf<String>()
        var i = 0
        while (i < input.length) {
            // Skip whitespace
            while (i < input.length && input[i] == ' ') i++
            if (i >= input.length) break

            if (input[i] == '"') {
                // Quoted string
                i++ // skip opening quote
                val sb = StringBuilder()
                while (i < input.length && input[i] != '"') {
                    if (input[i] == '\\' && i + 1 < input.length) {
                        i++
                        sb.append(input[i])
                    } else {
                        sb.append(input[i])
                    }
                    i++
                }
                if (i < input.length) i++ // skip closing quote
                tokens.add(sb.toString())
            } else {
                // Unquoted token
                val start = i
                while (i < input.length && input[i] != ' ') i++
                tokens.add(input.substring(start, i))
            }
        }
        return tokens
    }
}
