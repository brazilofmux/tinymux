package org.tinymux.titan.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

data class World(
    val name: String,
    val host: String,
    val port: Int = 4201,
    val ssl: Boolean = false,
    val character: String = "",
    val notes: String = "",
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("name", name)
        put("host", host)
        put("port", port)
        put("ssl", ssl)
        put("character", character)
        put("notes", notes)
    }

    companion object {
        fun fromJson(obj: JSONObject) = World(
            name = obj.optString("name", ""),
            host = obj.optString("host", ""),
            port = obj.optInt("port", 4201),
            ssl = obj.optBoolean("ssl", false),
            character = obj.optString("character", ""),
            notes = obj.optString("notes", ""),
        )
    }
}

class WorldRepository(context: Context) {
    private val prefs = context.getSharedPreferences("titan_worlds", Context.MODE_PRIVATE)
    private val key = "worlds_json"

    fun load(): List<World> {
        val raw = prefs.getString(key, null) ?: return emptyList()
        return try {
            val arr = JSONArray(raw)
            (0 until arr.length()).map { World.fromJson(arr.getJSONObject(it)) }
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun save(worlds: List<World>) {
        val arr = JSONArray()
        worlds.forEach { arr.put(it.toJson()) }
        prefs.edit().putString(key, arr.toString()).apply()
    }

    fun add(world: World) {
        val worlds = load().toMutableList()
        val idx = worlds.indexOfFirst { it.name == world.name }
        if (idx >= 0) worlds[idx] = world else worlds.add(world)
        save(worlds)
    }

    fun remove(name: String) {
        save(load().filter { it.name != name })
    }

    fun get(name: String): World? = load().find { it.name == name }
}
