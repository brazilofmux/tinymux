package org.tinymux.titan.data

import android.content.Context
import android.content.SharedPreferences
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import org.json.JSONArray
import org.json.JSONObject

data class World(
    val name: String,
    val host: String,
    val port: Int = 4201,
    val ssl: Boolean = false,
    val character: String = "",
    val notes: String = "",
    val loginCommands: List<String> = emptyList(),
    // Hydra proxy fields
    val useHydra: Boolean = false,
    val hydraUser: String = "",
    val hydraPass: String = "",
    val hydraGame: String = "",
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("name", name)
        put("host", host)
        put("port", port)
        put("ssl", ssl)
        put("character", character)
        put("notes", notes)
        put("loginCommands", JSONArray(loginCommands))
        if (useHydra) {
            put("useHydra", true)
            put("hydraUser", hydraUser)
            put("hydraPass", hydraPass)
            put("hydraGame", hydraGame)
        }
    }

    companion object {
        fun fromJson(obj: JSONObject) = World(
            name = obj.optString("name", ""),
            host = obj.optString("host", ""),
            port = obj.optInt("port", 4201),
            ssl = obj.optBoolean("ssl", false),
            character = obj.optString("character", ""),
            notes = obj.optString("notes", ""),
            loginCommands = obj.optJSONArray("loginCommands")?.let { arr ->
                (0 until arr.length()).map { arr.optString(it, "") }.filter { it.isNotBlank() }
            } ?: emptyList(),
            useHydra = obj.optBoolean("useHydra", false),
            hydraUser = obj.optString("hydraUser", ""),
            hydraPass = obj.optString("hydraPass", ""),
            hydraGame = obj.optString("hydraGame", ""),
        )
    }
}

class WorldRepository(context: Context) {
    private val prefs: SharedPreferences = try {
        val masterKey = MasterKey.Builder(context)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()
        EncryptedSharedPreferences.create(
            context,
            "titan_worlds_encrypted",
            masterKey,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
        )
    } catch (_: Exception) {
        // Fallback to unencrypted if Keystore unavailable (e.g. emulator)
        context.getSharedPreferences("titan_worlds", Context.MODE_PRIVATE)
    }

    private val key = "worlds_json"

    // Migrate from old unencrypted prefs on first run
    init {
        val oldPrefs = context.getSharedPreferences("titan_worlds", Context.MODE_PRIVATE)
        val oldData = oldPrefs.getString(key, null)
        if (oldData != null && prefs.getString(key, null) == null) {
            prefs.edit().putString(key, oldData).apply()
            oldPrefs.edit().remove(key).apply()
        }
    }

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
