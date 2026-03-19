package org.tinymux.titan.net

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration

object AnsiParser {
    private val ANSI_RE = Regex("\u001b\\[[0-9;]*[A-Za-z]")
    private val URL_RE = Regex("https?://[^\\s<>\"']+", RegexOption.IGNORE_CASE)

    fun stripAnsi(text: String): String = ANSI_RE.replace(text, "")

    private val COLORS_16 = arrayOf(
        Color(0xFF000000), Color(0xFFAA0000), Color(0xFF00AA00), Color(0xFFAA5500),
        Color(0xFF0000AA), Color(0xFFAA00AA), Color(0xFF00AAAA), Color(0xFFAAAAAA),
        Color(0xFF555555), Color(0xFFFF5555), Color(0xFF55FF55), Color(0xFFFFFF55),
        Color(0xFF5555FF), Color(0xFFFF55FF), Color(0xFF55FFFF), Color(0xFFFFFFFF),
    )

    fun parse(text: String): AnnotatedString {
        val builder = AnnotatedString.Builder()
        var fg: Color? = null
        var bg: Color? = null
        var bold = false
        var underline = false
        var inverse = false
        var i = 0

        fun pushStyle() {
            val efg = if (inverse) (bg ?: Color(0xFF000000)) else (fg ?: Color(0xFFC0C0C0))
            val ebg = if (inverse) (fg ?: Color(0xFFC0C0C0)) else bg
            builder.pushStyle(SpanStyle(
                color = efg,
                background = ebg ?: Color.Transparent,
                fontWeight = if (bold) FontWeight.Bold else FontWeight.Normal,
                textDecoration = if (underline) TextDecoration.Underline else TextDecoration.None,
            ))
        }

        var styleOpen = false

        while (i < text.length) {
            if (text[i] == '\u001b' && i + 1 < text.length && text[i + 1] == '[') {
                if (styleOpen) { builder.pop(); styleOpen = false }
                i += 2
                val params = StringBuilder()
                while (i < text.length && text[i] in '0'..'9' || (i < text.length && text[i] == ';')) {
                    params.append(text[i]); i++
                }
                if (i < text.length) i++ // skip final byte (m)

                val codes = if (params.isEmpty()) intArrayOf(0)
                else params.split(";").map { it.toIntOrNull() ?: 0 }.toIntArray()

                var j = 0
                while (j < codes.size) {
                    when (val c = codes[j]) {
                        0 -> { fg = null; bg = null; bold = false; underline = false; inverse = false }
                        1 -> bold = true
                        4 -> underline = true
                        7 -> inverse = true
                        22 -> bold = false
                        24 -> underline = false
                        27 -> inverse = false
                        in 30..37 -> fg = COLORS_16[c - 30 + if (bold) 8 else 0]
                        38 -> if (j + 1 < codes.size && codes[j + 1] == 5 && j + 2 < codes.size) {
                            fg = xterm256(codes[j + 2]); j += 2
                        } else if (j + 1 < codes.size && codes[j + 1] == 2 && j + 4 < codes.size) {
                            fg = Color(codes[j + 2], codes[j + 3], codes[j + 4]); j += 4
                        }
                        39 -> fg = null
                        in 40..47 -> bg = COLORS_16[c - 40]
                        48 -> if (j + 1 < codes.size && codes[j + 1] == 5 && j + 2 < codes.size) {
                            bg = xterm256(codes[j + 2]); j += 2
                        } else if (j + 1 < codes.size && codes[j + 1] == 2 && j + 4 < codes.size) {
                            bg = Color(codes[j + 2], codes[j + 3], codes[j + 4]); j += 4
                        }
                        49 -> bg = null
                        in 90..97 -> fg = COLORS_16[c - 90 + 8]
                        in 100..107 -> bg = COLORS_16[c - 100 + 8]
                    }
                    j++
                }
            } else {
                if (!styleOpen && (fg != null || bg != null || bold || underline || inverse)) {
                    pushStyle(); styleOpen = true
                }
                builder.append(text[i])
                i++
            }
        }
        if (styleOpen) builder.pop()

        // Add URL annotations for clickable links
        val result = builder.toAnnotatedString()
        val plainText = result.text
        val urlMatches = URL_RE.findAll(plainText).toList()
        if (urlMatches.isEmpty()) return result

        val annotated = AnnotatedString.Builder(result)
        for (match in urlMatches) {
            annotated.addStyle(
                SpanStyle(color = Color(0xFF6699FF), textDecoration = TextDecoration.Underline),
                match.range.first, match.range.last + 1
            )
            annotated.addStringAnnotation("URL", match.value, match.range.first, match.range.last + 1)
        }
        return annotated.toAnnotatedString()
    }

    private fun xterm256(idx: Int): Color {
        if (idx < 16) return COLORS_16[idx]
        if (idx < 232) {
            val n = idx - 16
            val r = (n / 36) * 51; val g = ((n / 6) % 6) * 51; val b = (n % 6) * 51
            return Color(r, g, b)
        }
        val v = (idx - 232) * 10 + 8
        return Color(v, v, v)
    }
}
