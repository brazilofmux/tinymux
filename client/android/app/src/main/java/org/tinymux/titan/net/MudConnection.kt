package org.tinymux.titan.net

import kotlinx.coroutines.*
import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.security.SecureRandom
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

class MudConnection(
    val name: String,
    val host: String,
    val port: Int,
    val useSsl: Boolean
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
                    // Trust all certificates for MUD servers (many use self-signed).
                    // TODO: Replace with trust-on-first-use (TOFU) pinning.
                    val trustAll = arrayOf<TrustManager>(object : X509TrustManager {
                        override fun checkClientTrusted(chain: Array<X509Certificate>?, type: String?) {}
                        override fun checkServerTrusted(chain: Array<X509Certificate>?, type: String?) {}
                        override fun getAcceptedIssuers(): Array<X509Certificate> = arrayOf()
                    })
                    val sslContext = SSLContext.getInstance("TLS")
                    sslContext.init(null, trustAll, SecureRandom())
                    val factory = sslContext.socketFactory
                    factory.createSocket(raw, host, port, true)
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
