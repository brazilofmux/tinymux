package org.tinymux.titan.net

import kotlinx.coroutines.*
import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.security.SecureRandom
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

data class CertInfo(
    val host: String,
    val port: Int,
    val fingerprint: String,
    val subject: String,
    val issuer: String,
    val savedFingerprint: String?,
)

class MudConnection(
    val name: String,
    val host: String,
    val port: Int,
    val useSsl: Boolean,
    private val certStore: TofuCertStore? = null,
) {
    private var socket: Socket? = null
    private var output: OutputStream? = null
    private var readJob: Job? = null

    val telnet = TelnetParser(::sendRaw)
    var connected = false; private set
    val scrollback = mutableListOf<String>()
    private val maxScrollback = 20000

    var onLine: ((String) -> Unit)? = null
    var onConnect: (() -> Unit)? = null
    var onDisconnect: (() -> Unit)? = null

    // TOFU callback: called on IO thread with cert info.
    // Must return true to accept, false to reject.
    // The callback should suspend (e.g. show a dialog and wait for user response).
    var onCertVerify: (suspend (CertInfo) -> Boolean)? = null

    fun connect(scope: CoroutineScope) {
        val mainDispatcher = Dispatchers.Main

        telnet.onLine = { line ->
            addScrollback(line)
            scope.launch(mainDispatcher) { onLine?.invoke(line) }
        }
        telnet.onPrompt = { prompt ->
            addScrollback(prompt)
            scope.launch(mainDispatcher) { onLine?.invoke(prompt) }
        }

        scope.launch(Dispatchers.IO) {
            try {
                val raw = Socket()
                raw.connect(InetSocketAddress(host, port), 10000)

                socket = if (useSsl) {
                    val tofuTrust = arrayOf<TrustManager>(object : X509TrustManager {
                        override fun checkClientTrusted(chain: Array<X509Certificate>?, type: String?) {}
                        override fun checkServerTrusted(chain: Array<X509Certificate>?, type: String?) {
                            // Accept all during handshake; we verify after.
                        }
                        override fun getAcceptedIssuers(): Array<X509Certificate> = arrayOf()
                    })
                    val sslContext = SSLContext.getInstance("TLS")
                    sslContext.init(null, tofuTrust, SecureRandom())
                    val sslSocket = sslContext.socketFactory.createSocket(raw, host, port, true)
                            as javax.net.ssl.SSLSocket
                    sslSocket.startHandshake()

                    // TOFU verification after handshake
                    val peerCerts = sslSocket.session.peerCertificates
                    if (peerCerts.isNotEmpty() && peerCerts[0] is X509Certificate) {
                        val cert = peerCerts[0] as X509Certificate
                        val fp = TofuCertStore.fingerprint(cert)
                        val saved = certStore?.getFingerprint(host, port)

                        if (saved != null && saved == fp) {
                            // Known cert, matches — proceed
                        } else {
                            // Unknown or changed cert — ask user
                            val info = CertInfo(
                                host = host, port = port,
                                fingerprint = fp,
                                subject = cert.subjectX500Principal.name,
                                issuer = cert.issuerX500Principal.name,
                                savedFingerprint = saved,
                            )
                            val accepted = onCertVerify?.invoke(info) ?: true
                            if (accepted) {
                                certStore?.saveFingerprint(host, port, fp)
                            } else {
                                sslSocket.close()
                                throw CertificateException("Certificate rejected by user")
                            }
                        }
                    }
                    sslSocket
                } else raw

                output = socket!!.getOutputStream()
                connected = true

                // Initial telnet negotiations
                sendRaw(byteArrayOf(0xFF.toByte(), 0xFB.toByte(), 31)) // WILL NAWS
                sendRaw(byteArrayOf(0xFF.toByte(), 0xFB.toByte(), 24)) // WILL TTYPE
                sendRaw(byteArrayOf(0xFF.toByte(), 0xFB.toByte(), 42)) // WILL CHARSET
                sendRaw(byteArrayOf(0xFF.toByte(), 0xFD.toByte(), 3))  // DO SGA
                sendRaw(byteArrayOf(0xFF.toByte(), 0xFD.toByte(), 1))  // DO ECHO

                launch(mainDispatcher) { onConnect?.invoke() }

                // Read loop
                val buf = ByteArray(8192)
                val input: InputStream = socket!!.getInputStream()
                while (connected) {
                    val n = input.read(buf)
                    if (n <= 0) break
                    telnet.process(buf, 0, n)
                }
            } catch (_: Exception) {
                // Connection failed or lost
            } finally {
                connected = false
                try { socket?.close() } catch (_: Exception) {}
                socket = null; output = null
                try { launch(mainDispatcher) { onDisconnect?.invoke() } } catch (_: Exception) {}
            }
        }.also { readJob = it }
    }

    fun disconnect() {
        connected = false
        readJob?.cancel()
        try { socket?.close() } catch (_: Exception) {}
    }

    fun sendLine(text: String) {
        if (!connected) return
        CoroutineScope(Dispatchers.IO).launch {
            try { telnet.sendLine(text) } catch (_: Exception) {}
        }
    }

    fun sendNaws(width: Int, height: Int) {
        telnet.nawsWidth = width
        telnet.nawsHeight = height
        telnet.sendNaws()
    }

    private fun sendRaw(data: ByteArray) {
        try { output?.write(data); output?.flush() } catch (_: Exception) {}
    }

    private fun addScrollback(line: String) {
        scrollback.add(line)
        while (scrollback.size > maxScrollback) scrollback.removeAt(0)
    }
}
