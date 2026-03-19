package org.tinymux.titan.data

data class TriggerResult(
    val matched: Boolean,
    val gagged: Boolean,
    val commands: List<String>,
    val displayLine: String?,
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

    fun check(line: String, context: ConditionContext = ConditionContext()): TriggerResult {
        var gagged = false
        val commands = mutableListOf<String>()
        var displayLine: String? = null
        var matched = false

        for ((trigger, regex) in compiled) {
            if (regex == null || trigger.shots == 0) continue

            val match = regex.find(line) ?: continue

            // Evaluate composite conditions if present
            if (trigger.conditions.isNotEmpty()) {
                val conditionsPassed = if (trigger.conditionsAnded) {
                    trigger.conditions.all { it.evaluate(line, context) }
                } else {
                    trigger.conditions.any { it.evaluate(line, context) }
                }
                if (!conditionsPassed) continue
            }

            matched = true

            if (trigger.gag) gagged = true

            if (trigger.hilite && !gagged) {
                val target = displayLine ?: line
                val m = regex.find(target)
                if (m != null) {
                    displayLine = buildString {
                        append(target.substring(0, m.range.first))
                        append("\u001b[1m")
                        append(m.value)
                        append("\u001b[22m")
                        append(target.substring(m.range.last + 1))
                    }
                }
            }

            // Substitution: regex find/replace on display text
            if (trigger.substituteFind.isNotBlank() && !gagged) {
                val target = displayLine ?: line
                try {
                    val subRe = Regex(trigger.substituteFind, RegexOption.IGNORE_CASE)
                    displayLine = subRe.replace(target, trigger.substituteReplace)
                } catch (_: Exception) {}
            }

            if (trigger.body.isNotBlank()) {
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
            displayLine = displayLine,
        )
    }
}
