package org.tinymux.titan.data

import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

// MARK: - Variable Store

class VariableStore {
    // User-defined session variables (temp.*)
    val temp = mutableMapOf<String, String>()

    // Per-world session variables (worldtemp.*)
    val worldTemp = mutableMapOf<String, String>()

    // Last regex match captures (regexp.*)
    var regexpCaptures = listOf<String>()

    // Resolve a variable reference like "world.name" or "datetime.time"
    fun resolve(key: String, worldName: String = "", character: String = "",
                host: String = "", port: Int = 0, connected: Boolean = false,
                eventLine: String = "", eventCause: String = "line"): String? {
        val parts = key.split(".", limit = 2)
        if (parts.size < 2) return temp[key]

        val namespace = parts[0]
        val name = parts[1]

        return when (namespace) {
            "world" -> resolveWorld(name, worldName, character, host, port, connected)
            "event" -> resolveEvent(name, eventLine, eventCause)
            "regexp" -> resolveRegexp(name)
            "datetime" -> resolveDatetime(name)
            "temp" -> temp[name]
            "worldtemp" -> worldTemp[name]
            else -> null
        }
    }

    private fun resolveWorld(name: String, worldName: String, character: String,
                             host: String, port: Int, connected: Boolean): String? {
        return when (name) {
            "name" -> worldName
            "character" -> character
            "host" -> host
            "port" -> port.toString()
            "connected" -> if (connected) "1" else "0"
            else -> null
        }
    }

    private fun resolveEvent(name: String, line: String, cause: String): String? {
        return when (name) {
            "line" -> line
            "cause" -> cause
            else -> null
        }
    }

    private fun resolveRegexp(name: String): String? {
        val idx = name.toIntOrNull() ?: return null
        return regexpCaptures.getOrNull(idx)
    }

    private fun resolveDatetime(name: String): String? {
        val now = Date()
        return when (name) {
            "date" -> SimpleDateFormat("yyyy-MM-dd", Locale.US).format(now)
            "time" -> SimpleDateFormat("HH:mm:ss", Locale.US).format(now)
            "year" -> SimpleDateFormat("yyyy", Locale.US).format(now)
            "month" -> SimpleDateFormat("MM", Locale.US).format(now)
            "day" -> SimpleDateFormat("dd", Locale.US).format(now)
            "hour" -> SimpleDateFormat("HH", Locale.US).format(now)
            "minute" -> SimpleDateFormat("mm", Locale.US).format(now)
            "second" -> SimpleDateFormat("ss", Locale.US).format(now)
            "weekday" -> SimpleDateFormat("EEEE", Locale.US).format(now)
            "weekdayshort" -> SimpleDateFormat("EEE", Locale.US).format(now)
            "monthname" -> SimpleDateFormat("MMMM", Locale.US).format(now)
            "monthnameshort" -> SimpleDateFormat("MMM", Locale.US).format(now)
            else -> null
        }
    }

    // Expand $var.name references in a string
    fun expand(text: String, worldName: String = "", character: String = "",
               host: String = "", port: Int = 0, connected: Boolean = false,
               eventLine: String = "", eventCause: String = "line"): String {
        return text.replace(Regex("\\$([a-zA-Z_][a-zA-Z0-9_.]+)")) { match ->
            resolve(match.groupValues[1], worldName, character, host, port,
                    connected, eventLine, eventCause) ?: match.value
        }
    }
}
