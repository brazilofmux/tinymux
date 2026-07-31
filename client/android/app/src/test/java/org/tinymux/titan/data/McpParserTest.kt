package org.tinymux.titan.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * #1893: multiline reassembly caps (mirrors web #1889 / iOS McpParserTests).
 */
class McpParserTest {

    @Test
    fun pendingMultilineCountIsCapped() {
        val parser = McpParser()
        parser.sessionKey = "key"
        val diags = mutableListOf<String>()
        parser.onDiagnostic = { diags.add(it) }

        for (i in 0 until 40) {
            parser.processLine(
                "#\$#dns-org-mud-moo-simpleedit-content key reference: r content*: \"\" _data-tag: t$i"
            )
        }
        assertTrue(
            "pending multiline count must be capped, got ${parser.pendingCount}",
            parser.pendingCount <= MCP_MAX_PENDING_MESSAGES
        )
        assertTrue(
            "should diagnose pending eviction",
            diags.any { it.contains(Regex("evict|capacity|pending", RegexOption.IGNORE_CASE)) }
        )
    }

    @Test
    fun oversizedContinuationDropsTag() {
        val parser = McpParser()
        parser.sessionKey = "key"
        val diags = mutableListOf<String>()
        parser.onDiagnostic = { diags.add(it) }

        parser.processLine(
            "#\$#dns-org-mud-moo-simpleedit-content key reference: r content*: \"\" _data-tag: fat"
        )
        assertTrue(parser.isPending("fat"))

        val chunk = "x".repeat(1024)
        for (i in 0 until 300) {
            parser.processLine("#\$#* fat content: $chunk")
            if (!parser.isPending("fat")) break
        }
        assertFalse("oversized multiline must be dropped", parser.isPending("fat"))
        assertTrue(
            "should diagnose per-message size drop",
            diags.any { it.contains(Regex("size limit|dropped", RegexOption.IGNORE_CASE)) }
        )
    }

    @Test
    fun completeMultilineStillDispatches() {
        val parser = McpParser()
        parser.sessionKey = "key"
        var edit: Quadruple? = null
        parser.onEditRequest = { ref, name, type, content ->
            edit = Quadruple(ref, name, type, content)
        }

        parser.processLine(
            "#\$#dns-org-mud-moo-simpleedit-content key reference: R name: N type: string-list content*: \"\" _data-tag: ok"
        )
        parser.processLine("#\$#* ok content: line1")
        parser.processLine("#\$#* ok content: line2")
        parser.processLine("#\$#: ok")

        assertNotNull(edit)
        assertEquals("line1\nline2", edit!!.content)
        assertEquals(0, parser.pendingCount)
    }

    private data class Quadruple(
        val ref: String,
        val name: String,
        val type: String,
        val content: String,
    )
}
