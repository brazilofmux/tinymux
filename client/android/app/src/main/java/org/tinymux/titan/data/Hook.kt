package org.tinymux.titan.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

data class Hook(
    val name: String,
    val event: String,
    val body: String,
    val enabled: Boolean = true,
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("name", name)
        put("event", event)
        put("body", body)
        put("enabled", enabled)
    }

    companion object {
        val EVENTS = listOf("CONNECT", "DISCONNECT", "ACTIVITY")

        fun fromJson(obj: JSONObject) = Hook(
            name = obj.optString("name", ""),
            event = obj.optString("event", ""),
            body = obj.optString("body", ""),
            enabled = obj.optBoolean("enabled", true),
        )
    }
}

class HookRepository(context: Context) {
    private val prefs = context.getSharedPreferences("titan_hooks", Context.MODE_PRIVATE)
    private val key = "hooks_json"

    fun load(): List<Hook> {
        val raw = prefs.getString(key, null) ?: return emptyList()
        return try {
            val arr = JSONArray(raw)
            (0 until arr.length()).map { Hook.fromJson(arr.getJSONObject(it)) }
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun save(hooks: List<Hook>) {
        val arr = JSONArray()
        hooks.forEach { arr.put(it.toJson()) }
        prefs.edit().putString(key, arr.toString()).apply()
    }

    fun add(hook: Hook) {
        val list = load().toMutableList()
        val idx = list.indexOfFirst { it.name == hook.name }
        if (idx >= 0) list[idx] = hook else list.add(hook)
        save(list)
    }

    fun remove(name: String) {
        save(load().filter { it.name != name })
    }

    fun fireEvent(event: String): List<String> {
        return load()
            .filter { it.enabled && it.event.equals(event, ignoreCase = true) }
            .map { it.body }
    }
}
