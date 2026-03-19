package org.tinymux.titan.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

data class Trigger(
    val name: String,
    val pattern: String,
    val body: String = "",
    val priority: Int = 0,
    val shots: Int = -1,
    val gag: Boolean = false,
    val hilite: Boolean = false,
    val enabled: Boolean = true,
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("name", name)
        put("pattern", pattern)
        put("body", body)
        put("priority", priority)
        put("shots", shots)
        put("gag", gag)
        put("hilite", hilite)
        put("enabled", enabled)
    }

    companion object {
        fun fromJson(obj: JSONObject) = Trigger(
            name = obj.optString("name", ""),
            pattern = obj.optString("pattern", ""),
            body = obj.optString("body", ""),
            priority = obj.optInt("priority", 0),
            shots = obj.optInt("shots", -1),
            gag = obj.optBoolean("gag", false),
            hilite = obj.optBoolean("hilite", false),
            enabled = obj.optBoolean("enabled", true),
        )
    }
}

class TriggerRepository(context: Context) {
    private val prefs = context.getSharedPreferences("titan_triggers", Context.MODE_PRIVATE)
    private val key = "triggers_json"

    fun load(): List<Trigger> {
        val raw = prefs.getString(key, null) ?: return emptyList()
        return try {
            val arr = JSONArray(raw)
            (0 until arr.length()).map { Trigger.fromJson(arr.getJSONObject(it)) }
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun save(triggers: List<Trigger>) {
        val arr = JSONArray()
        triggers.forEach { arr.put(it.toJson()) }
        prefs.edit().putString(key, arr.toString()).apply()
    }

    fun add(trigger: Trigger) {
        val list = load().toMutableList()
        val idx = list.indexOfFirst { it.name == trigger.name }
        if (idx >= 0) list[idx] = trigger else list.add(trigger)
        list.sortByDescending { it.priority }
        save(list)
    }

    fun remove(name: String) {
        save(load().filter { it.name != name })
    }
}
