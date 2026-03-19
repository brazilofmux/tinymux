package org.tinymux.titan.data

data class TriggerResult(
    val matched: Boolean,
    val gagged: Boolean,
    val commands: List<String>,
    val hiliteLine: String?,
)

class TriggerEngine {
    private var triggers: List<Trigger> = emptyList()
    private var compiled: List<Pair<Trigger, Regex?>> = emptyList()

    fun load(triggers: List<Trigger>) {
        this.triggers = triggers.sortedByDescending { it.priority }
        this.compiled = this.triggers.map { t ->
            val re = if (t.enabled && t.pattern.isNotBlank()) {
                try {
                    Regex(t.pattern, RegexOption.IGNORE_CASE)
                } catch (_: Exception) {
                    null
                }
            } else null
            t to re
        }
    }

    fun check(line: String): TriggerResult {
        var gagged = false
        val commands = mutableListOf<String>()
        var hiliteLine: String? = null
        var matched = false

        for ((trigger, regex) in compiled) {
            if (regex == null || trigger.shots == 0) continue

            val match = regex.find(line) ?: continue
            matched = true

            if (trigger.gag) gagged = true

            if (trigger.hilite && !gagged) {
                // Wrap matched portion in bold ANSI escape
                val hl = buildString {
                    append(line.substring(0, match.range.first))
                    append("\u001b[1m")
                    append(match.value)
                    append("\u001b[22m")
                    append(line.substring(match.range.last + 1))
                }
                hiliteLine = hl
            }

            if (trigger.body.isNotBlank()) {
                // Substitute capture groups: $0, $1, $2, ...
                var cmd = trigger.body
                match.groupValues.forEachIndexed { i, v ->
                    cmd = cmd.replace("\$$i", v)
                }
                commands.add(cmd)
            }
        }

        return TriggerResult(
            matched = matched,
            gagged = gagged,
            commands = commands,
            hiliteLine = hiliteLine,
        )
    }
}
