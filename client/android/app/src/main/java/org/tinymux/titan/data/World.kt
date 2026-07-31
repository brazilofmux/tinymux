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
    // Last known Hydra session id. Persisted so the next launch can resume via
    // GetSession(saved_id) instead of re-authenticating (#762). Stored in
    // EncryptedSharedPreferences alongside the password.
    val hydraSession: String = "",
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
            if (hydraSession.isNotEmpty()) put("hydraSession", hydraSession)
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
            hydraSession = obj.optString("hydraSession", ""),
        )
    }
}

/**
 * #1892: never write hydraPass / hydraSession to an unencrypted preference file.
 * When EncryptedSharedPreferences cannot be created, world metadata may still
 * use the plain store, but secrets are stripped on load and save.
 */
fun World.withoutSecrets(): World = copy(hydraPass = "", hydraSession = "")

/** Pure helper for tests — what [WorldRepository.save] actually persists. */
fun worldsForPersistence(worlds: List<World>, secureStorage: Boolean): List<World> =
    if (secureStorage) worlds else worlds.map { it.withoutSecrets() }

class WorldRepository(context: Context) {
    /**
     * True when the backing store is EncryptedSharedPreferences.
     * UI should warn when false so the user knows Hydra secrets cannot be kept.
     */
    val isSecureStorageAvailable: Boolean

    private val prefs: SharedPreferences
    private val key = "worlds_json"

    init {
        var secure = false
        var store: SharedPreferences
        try {
            val masterKey = MasterKey.Builder(context)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build()
            store = EncryptedSharedPreferences.create(
                context,
                "titan_worlds_encrypted",
                masterKey,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
            )
            secure = true
        } catch (e: Exception) {
            // #1892: do not silently treat this as a full equivalent of encrypted
            // storage.  Plain prefs may hold non-secret world fields only.
            android.util.Log.w(
                "WorldRepository",
                "EncryptedSharedPreferences unavailable; Hydra secrets will not be persisted",
                e,
            )
            store = context.getSharedPreferences("titan_worlds", Context.MODE_PRIVATE)
            secure = false
        }
        isSecureStorageAvailable = secure
        prefs = store

        if (secure) {
            // Migrate from old unencrypted prefs into the encrypted store once.
            val oldPrefs = context.getSharedPreferences("titan_worlds", Context.MODE_PRIVATE)
            val oldData = oldPrefs.getString(key, null)
            if (oldData != null && prefs.getString(key, null) == null) {
                prefs.edit().putString(key, oldData).apply()
                oldPrefs.edit().remove(key).apply()
            }
        } else {
            // Scrub any secrets that may already sit in the plain file (prior
            // silent-fallback builds, or a keystore failure after secrets were
            // written when encryption briefly worked).
            scrubPlaintextSecretsOnDisk()
        }
    }

    private fun scrubPlaintextSecretsOnDisk() {
        val raw = prefs.getString(key, null) ?: return
        try {
            val arr = JSONArray(raw)
            val worlds = (0 until arr.length()).map { World.fromJson(arr.getJSONObject(it)) }
            val cleaned = worldsForPersistence(worlds, secureStorage = false)
            val hadSecrets = worlds.any { it.hydraPass.isNotEmpty() || it.hydraSession.isNotEmpty() }
            if (hadSecrets) {
                val out = JSONArray()
                cleaned.forEach { out.put(it.toJson()) }
                prefs.edit().putString(key, out.toString()).apply()
            }
        } catch (_: Exception) {
            // Leave raw alone if unreadable; load() will return empty.
        }
    }

    fun load(): List<World> {
        val raw = prefs.getString(key, null) ?: return emptyList()
        return try {
            val arr = JSONArray(raw)
            val worlds = (0 until arr.length()).map { World.fromJson(arr.getJSONObject(it)) }
            // Never surface secrets from a non-secure store into the app.
            worldsForPersistence(worlds, isSecureStorageAvailable)
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun save(worlds: List<World>) {
        val toWrite = worldsForPersistence(worlds, isSecureStorageAvailable)
        val arr = JSONArray()
        toWrite.forEach { arr.put(it.toJson()) }
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
