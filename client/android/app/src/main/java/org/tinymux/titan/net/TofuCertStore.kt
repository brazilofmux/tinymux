package org.tinymux.titan.net

import android.content.Context
import java.security.MessageDigest
import java.security.cert.X509Certificate

class TofuCertStore(context: Context) {
    private val prefs = context.getSharedPreferences("titan_certs", Context.MODE_PRIVATE)

    fun getFingerprint(host: String, port: Int): String? {
        return prefs.getString(key(host, port), null)
    }

    fun saveFingerprint(host: String, port: Int, fingerprint: String) {
        prefs.edit().putString(key(host, port), fingerprint).apply()
    }

    fun removeFingerprint(host: String, port: Int) {
        prefs.edit().remove(key(host, port)).apply()
    }

    private fun key(host: String, port: Int) = "$host:$port"

    companion object {
        fun fingerprint(cert: X509Certificate): String {
            val md = MessageDigest.getInstance("SHA-256")
            val digest = md.digest(cert.encoded)
            return digest.joinToString(":") { "%02X".format(it) }
        }
    }
}
