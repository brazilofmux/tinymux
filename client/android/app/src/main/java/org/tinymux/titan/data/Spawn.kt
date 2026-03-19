package org.tinymux.titan.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

data class SpawnConfig(
    val name: String,
    val path: String,
    val patterns: List<String> = emptyList(),
    val exceptions: List<String> = emptyList(),
    val prefix: String = "",
    val maxLines: Int = 20000,
    val weight: Int = 0,
) {
    private val compiledPatterns by lazy {
        patterns.mapNotNull { p ->
            try { Regex(p, RegexOption.IGNORE_CASE) } catch (_: Exception) { null }
        }
    }
    private val compiledExceptions by lazy {
        exceptions.mapNotNull { p ->
            try { Regex(p, RegexOption.IGNORE_CASE) } catch (_: Exception) { null }
        }
    }

    fun matches(line: String): Boolean {
        if (compiledPatterns.isEmpty()) return false
        val matchesPattern = compiledPatterns.any { it.containsMatchIn(line) }
        if (!matchesPattern) return false
        val matchesException = compiledExceptions.any { it.containsMatchIn(line) }
        return !matchesException
    }

    fun toJson(): JSONObject = JSONObject().apply {
        put("name", name)
        put("path", path)
        put("patterns", JSONArray(patterns))
        put("exceptions", JSONArray(exceptions))
        put("prefix", prefix)
        put("maxLines", maxLines)
        put("weight", weight)
    }

    companion object {
        fun fromJson(obj: JSONObject) = SpawnConfig(
            name = obj.optString("name", ""),
            path = obj.optString("path", ""),
            patterns = obj.optJSONArray("patterns")?.let { arr ->
                (0 until arr.length()).map { arr.optString(it, "") }.filter { it.isNotBlank() }
            } ?: emptyList(),
            exceptions = obj.optJSONArray("exceptions")?.let { arr ->
                (0 until arr.length()).map { arr.optString(it, "") }.filter { it.isNotBlank() }
            } ?: emptyList(),
            prefix = obj.optString("prefix", ""),
            maxLines = obj.optInt("maxLines", 20000),
            weight = obj.optInt("weight", 0),
        )
    }
}

class SpawnRepository(context: Context) {
    private val prefs = context.getSharedPreferences("titan_spawns", Context.MODE_PRIVATE)
    private val key = "spawns_json"

    fun load(): List<SpawnConfig> {
        val raw = prefs.getString(key, null) ?: return emptyList()
        return try {
            val arr = JSONArray(raw)
            (0 until arr.length()).map { SpawnConfig.fromJson(arr.getJSONObject(it)) }
                .sortedBy { it.weight }
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun save(spawns: List<SpawnConfig>) {
        val arr = JSONArray()
        spawns.forEach { arr.put(it.toJson()) }
        prefs.edit().putString(key, arr.toString()).apply()
    }

    fun add(spawn: SpawnConfig) {
        val list = load().toMutableList()
        val idx = list.indexOfFirst { it.path == spawn.path }
        if (idx >= 0) list[idx] = spawn else list.add(spawn)
        list.sortBy { it.weight }
        save(list)
    }

    fun remove(path: String) {
        save(load().filter { it.path != path })
    }
}
