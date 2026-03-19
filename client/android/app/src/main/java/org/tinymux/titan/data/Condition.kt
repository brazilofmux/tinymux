package org.tinymux.titan.data

import org.json.JSONArray
import org.json.JSONObject

// MARK: - Condition Types

sealed class TriggerCondition {
    abstract fun evaluate(line: String, context: ConditionContext): Boolean
    abstract fun toJson(): JSONObject

    // String match — regex against the line
    data class StringMatch(val pattern: String, val negate: Boolean = false) : TriggerCondition() {
        private val regex by lazy {
            try { Regex(pattern, RegexOption.IGNORE_CASE) } catch (_: Exception) { null }
        }

        override fun evaluate(line: String, context: ConditionContext): Boolean {
            val matches = regex?.containsMatchIn(line) ?: false
            return if (negate) !matches else matches
        }

        override fun toJson() = JSONObject().apply {
            put("type", "stringMatch")
            put("pattern", pattern)
            put("negate", negate)
        }
    }

    // World is connected
    data class WorldConnected(val negate: Boolean = false) : TriggerCondition() {
        override fun evaluate(line: String, context: ConditionContext): Boolean {
            val connected = context.isConnected
            return if (negate) !connected else connected
        }

        override fun toJson() = JSONObject().apply {
            put("type", "worldConnected")
            put("negate", negate)
        }
    }

    // World idle for N seconds
    data class WorldIdle(val seconds: Int, val negate: Boolean = false) : TriggerCondition() {
        override fun evaluate(line: String, context: ConditionContext): Boolean {
            val idle = context.idleSeconds >= seconds
            return if (negate) !idle else idle
        }

        override fun toJson() = JSONObject().apply {
            put("type", "worldIdle")
            put("seconds", seconds)
            put("negate", negate)
        }
    }

    // Composite group — AND/OR of child conditions
    data class Group(
        val conditions: List<TriggerCondition>,
        val anded: Boolean = true,
    ) : TriggerCondition() {
        override fun evaluate(line: String, context: ConditionContext): Boolean {
            return if (anded) {
                conditions.all { it.evaluate(line, context) }
            } else {
                conditions.any { it.evaluate(line, context) }
            }
        }

        override fun toJson() = JSONObject().apply {
            put("type", "group")
            put("anded", anded)
            put("conditions", JSONArray().apply {
                conditions.forEach { put(it.toJson()) }
            })
        }
    }

    // NOT wrapper
    data class Negate(val condition: TriggerCondition) : TriggerCondition() {
        override fun evaluate(line: String, context: ConditionContext): Boolean {
            return !condition.evaluate(line, context)
        }

        override fun toJson() = JSONObject().apply {
            put("type", "negate")
            put("condition", condition.toJson())
        }
    }

    // Line has a specific class assigned by a prior trigger
    data class LineClass(val className: String, val negate: Boolean = false) : TriggerCondition() {
        override fun evaluate(line: String, context: ConditionContext): Boolean {
            val has = context.lineClasses.contains(className)
            return if (negate) !has else has
        }

        override fun toJson() = JSONObject().apply {
            put("type", "lineClass")
            put("className", className)
            put("negate", negate)
        }
    }

    companion object {
        fun fromJson(obj: JSONObject): TriggerCondition? {
            return when (obj.optString("type")) {
                "stringMatch" -> StringMatch(
                    pattern = obj.optString("pattern", ""),
                    negate = obj.optBoolean("negate", false),
                )
                "worldConnected" -> WorldConnected(
                    negate = obj.optBoolean("negate", false),
                )
                "worldIdle" -> WorldIdle(
                    seconds = obj.optInt("seconds", 60),
                    negate = obj.optBoolean("negate", false),
                )
                "lineClass" -> LineClass(
                    className = obj.optString("className", ""),
                    negate = obj.optBoolean("negate", false),
                )
                "group" -> {
                    val arr = obj.optJSONArray("conditions") ?: return null
                    val children = (0 until arr.length()).mapNotNull {
                        fromJson(arr.getJSONObject(it))
                    }
                    Group(conditions = children, anded = obj.optBoolean("anded", true))
                }
                "negate" -> {
                    val child = obj.optJSONObject("condition") ?: return null
                    val inner = fromJson(child) ?: return null
                    Negate(inner)
                }
                else -> null
            }
        }
    }
}

// Context passed to condition evaluation
data class ConditionContext(
    val isConnected: Boolean = false,
    val idleSeconds: Long = 0,
    val lineClasses: MutableSet<String> = mutableSetOf(),
)
