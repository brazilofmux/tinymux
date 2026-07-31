package org.tinymux.titan.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * #1892: secrets must not be scheduled for plaintext persistence when
 * EncryptedSharedPreferences is unavailable.
 */
class WorldRepositorySecurityTest {

    private fun sampleWorld(
        pass: String = "s3cret",
        session: String = "sess-token",
    ) = World(
        name = "Test",
        host = "example.com",
        useHydra = true,
        hydraUser = "alice",
        hydraPass = pass,
        hydraGame = "mux",
        hydraSession = session,
    )

    @Test
    fun secureStorageKeepsSecrets() {
        val worlds = listOf(sampleWorld())
        val out = worldsForPersistence(worlds, secureStorage = true)
        assertEquals(1, out.size)
        assertEquals("s3cret", out[0].hydraPass)
        assertEquals("sess-token", out[0].hydraSession)
    }

    @Test
    fun insecureStorageStripsSecrets() {
        val worlds = listOf(sampleWorld())
        val out = worldsForPersistence(worlds, secureStorage = false)
        assertEquals(1, out.size)
        assertEquals("", out[0].hydraPass)
        assertEquals("", out[0].hydraSession)
        // Non-secret fields retained.
        assertEquals("alice", out[0].hydraUser)
        assertEquals("mux", out[0].hydraGame)
        assertTrue(out[0].useHydra)
    }

    @Test
    fun withoutSecretsIsIdempotent() {
        val w = sampleWorld().withoutSecrets()
        assertEquals("", w.hydraPass)
        assertEquals("", w.hydraSession)
        assertEquals(w, w.withoutSecrets())
    }
}
