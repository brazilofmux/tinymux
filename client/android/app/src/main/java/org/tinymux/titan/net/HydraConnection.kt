package org.tinymux.titan.net

import io.grpc.ManagedChannel
import io.grpc.ManagedChannelBuilder
import io.grpc.Metadata
import io.grpc.stub.AbstractStub
import hydra.HydraServiceGrpcKt.HydraServiceCoroutineStub
import hydra.Hydra.*
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.consumeAsFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.merge

/**
 * A connection to a game server via Hydra's gRPC GameSession bidi stream.
 * Presents the same callback interface as MudConnection so TitanApp can
 * use either transport interchangeably.
 */
class HydraConnection(
    val name: String,
    val host: String,
    val port: Int,
    private val username: String,
    private val password: String,
    private val gameName: String,
    private val useTls: Boolean = true,
) {
    var connected = false; private set
    val scrollback = mutableListOf<String>()
    private val maxScrollback = 20000

    var onLine: ((String) -> Unit)? = null
    var onConnect: (() -> Unit)? = null
    var onDisconnect: (() -> Unit)? = null

    private var channel: ManagedChannel? = null
    private var sessionId: String = ""
    private var sessionJob: Job? = null
    // Coroutine channel for sending input through the bidi stream
    private val inputChannel = Channel<ClientMessage>(Channel.BUFFERED)

    /** Attach authorization metadata to a gRPC stub. */
    private fun <S : AbstractStub<S>> S.withAuth(): S {
        val meta = Metadata().apply {
            put(Metadata.Key.of("authorization", Metadata.ASCII_STRING_MARSHALLER), sessionId)
        }
        return withInterceptors(io.grpc.stub.MetadataUtils.newAttachHeadersInterceptor(meta))
    }

    fun connect(scope: CoroutineScope) {
        val mainDispatcher = Dispatchers.Main

        scope.launch(Dispatchers.IO) {
            try {
                // Create gRPC channel (TLS by default)
                val builder = ManagedChannelBuilder.forAddress(host, port)
                if (!useTls) builder.usePlaintext()
                val ch = builder.build()
                channel = ch

                val stub = HydraServiceCoroutineStub(ch)

                // Authenticate
                val authResp = stub.authenticate(
                    AuthRequest.newBuilder()
                        .setUsername(username)
                        .setPassword(password)
                        .build()
                )

                if (!authResp.success) {
                    val err = authResp.error.ifEmpty { "Authentication failed" }
                    addScrollback("[Hydra] $err")
                    launch(mainDispatcher) { onLine?.invoke("[Hydra] $err") }
                    ch.shutdownNow()
                    channel = null
                    return@launch
                }

                sessionId = authResp.sessionId
                val authedStub = stub.withAuth()

                // Connect to game
                if (gameName.isNotEmpty()) {
                    val connResp = authedStub.connect(
                        ConnectRequest.newBuilder()
                            .setSessionId(sessionId)
                            .setGameName(gameName)
                            .build()
                    )
                    val msg = if (connResp.success) {
                        "[Hydra] Connected to $gameName (link ${connResp.linkNumber})"
                    } else {
                        "[Hydra] Game connect failed: ${connResp.error}"
                    }
                    addScrollback(msg)
                    launch(mainDispatcher) { onLine?.invoke(msg) }
                }

                connected = true
                val sessionMsg = "[Hydra] Session established (${sessionId.take(8)}...)"
                addScrollback(sessionMsg)
                launch(mainDispatcher) {
                    onLine?.invoke(sessionMsg)
                    onConnect?.invoke()
                }

                // Open bidi GameSession stream — input lines and pings
                // are both sent through this stream.
                val pings = flow {
                    while (connected) {
                        delay(60000)
                        emit(
                            ClientMessage.newBuilder()
                                .setPing(
                                    PingMessage.newBuilder()
                                        .setClientTimestamp(System.currentTimeMillis())
                                        .build()
                                )
                                .build()
                        )
                    }
                }
                val userInput = inputChannel.consumeAsFlow()
                @OptIn(kotlinx.coroutines.FlowPreview::class)
                val requests = merge(userInput, pings)

                val responses = authedStub.gameSession(requests)

                // Collect server messages
                responses.collect { msg ->
                    val payloadCase = msg.payloadCase
                    when (payloadCase) {
                        ServerMessage.PayloadCase.GAME_OUTPUT -> {
                            val text = msg.gameOutput.text
                            addScrollback(text)
                            launch(mainDispatcher) { onLine?.invoke(text) }
                        }
                        ServerMessage.PayloadCase.GMCP -> {
                            val text = "[GMCP ${msg.gmcp.`package`}] ${msg.gmcp.json}"
                            addScrollback(text)
                            launch(mainDispatcher) { onLine?.invoke(text) }
                        }
                        ServerMessage.PayloadCase.NOTICE -> {
                            val text = "[Hydra] ${msg.notice.text}"
                            addScrollback(text)
                            launch(mainDispatcher) { onLine?.invoke(text) }
                        }
                        ServerMessage.PayloadCase.LINK_EVENT -> {
                            val ev = msg.linkEvent
                            val text = "[Hydra] Link ${ev.linkNumber} (${ev.gameName}): ${ev.newState.name}"
                            addScrollback(text)
                            launch(mainDispatcher) { onLine?.invoke(text) }
                        }
                        ServerMessage.PayloadCase.PONG -> {
                            // Silently absorb pongs
                        }
                        else -> {}
                    }
                }
            } catch (_: Exception) {
                // Stream ended or error
            } finally {
                connected = false
                channel?.shutdownNow()
                channel = null
                launch(mainDispatcher) { onDisconnect?.invoke() }
            }
        }.also { sessionJob = it }
    }

    fun disconnect() {
        connected = false
        sessionJob?.cancel()
        channel?.shutdownNow()
        channel = null
    }

    fun sendLine(text: String) {
        if (!connected) return
        val msg = ClientMessage.newBuilder()
            .setInputLine(text)
            .build()
        inputChannel.trySend(msg)
    }

    // ---- Hydra session management RPCs ----

    suspend fun rpcConnectGame(gameName: String): String {
        val ch = channel ?: return "[Hydra] Not connected."
        return try {
            val stub = HydraServiceCoroutineStub(ch).withAuth()
            val resp = stub.connect(
                ConnectRequest.newBuilder().setSessionId(sessionId).setGameName(gameName).build()
            )
            if (resp.success) "[Hydra] Connected to $gameName (link ${resp.linkNumber})"
            else "[Hydra] Connect failed: ${resp.error}"
        } catch (e: Exception) { "[Hydra] Error: ${e.message}" }
    }

    suspend fun rpcSwitchLink(linkNumber: Int): String {
        val ch = channel ?: return "[Hydra] Not connected."
        return try {
            val stub = HydraServiceCoroutineStub(ch).withAuth()
            val resp = stub.switchLink(
                SwitchRequest.newBuilder().setSessionId(sessionId).setLinkNumber(linkNumber).build()
            )
            if (resp.success) "[Hydra] Switched to link $linkNumber"
            else "[Hydra] Switch failed: ${resp.error}"
        } catch (e: Exception) { "[Hydra] Error: ${e.message}" }
    }

    suspend fun rpcListLinks(): List<String> {
        val ch = channel ?: return listOf("[Hydra] Not connected.")
        return try {
            val stub = HydraServiceCoroutineStub(ch).withAuth()
            val resp = stub.listLinks(
                SessionRequest.newBuilder().setSessionId(sessionId).build()
            )
            if (resp.linksList.isEmpty()) return listOf("[Hydra] No active links.")
            val lines = mutableListOf("[Hydra] Active links:")
            for (li in resp.linksList) {
                var line = "  Link ${li.number}: ${li.gameName} (${li.state.name})"
                if (li.active) line += " [active]"
                if (li.character.isNotEmpty()) line += " as ${li.character}"
                lines.add(line)
            }
            lines
        } catch (e: Exception) { listOf("[Hydra] Error: ${e.message}") }
    }

    suspend fun rpcDisconnectLink(linkNumber: Int): String {
        val ch = channel ?: return "[Hydra] Not connected."
        return try {
            val stub = HydraServiceCoroutineStub(ch).withAuth()
            val resp = stub.disconnectLink(
                DisconnectRequest.newBuilder().setSessionId(sessionId).setLinkNumber(linkNumber).build()
            )
            if (resp.success) "[Hydra] Disconnected link $linkNumber"
            else "[Hydra] Disconnect failed: ${resp.error}"
        } catch (e: Exception) { "[Hydra] Error: ${e.message}" }
    }

    suspend fun rpcGetSession(): List<String> {
        val ch = channel ?: return listOf("[Hydra] Not connected.")
        return try {
            val stub = HydraServiceCoroutineStub(ch).withAuth()
            val resp = stub.getSession(
                SessionRequest.newBuilder().setSessionId(sessionId).build()
            )
            listOf(
                "[Hydra] Session ${resp.sessionId.take(8)}...",
                "  User: ${resp.username}",
                "  State: ${resp.state.name}",
                "  Active link: ${resp.activeLink}",
                "  Links: ${resp.linksCount}",
                "  Scrollback: ${resp.scrollbackLines} lines",
            )
        } catch (e: Exception) { listOf("[Hydra] Error: ${e.message}") }
    }

    suspend fun rpcDetachSession(): String {
        val ch = channel ?: return "[Hydra] Not connected."
        return try {
            val stub = HydraServiceCoroutineStub(ch).withAuth()
            stub.detachSession(
                SessionRequest.newBuilder().setSessionId(sessionId).build()
            )
            connected = false
            "[Hydra] Session detached. Reconnect to resume."
        } catch (e: Exception) { "[Hydra] Error: ${e.message}" }
    }

    private fun addScrollback(line: String) {
        scrollback.add(line)
        while (scrollback.size > maxScrollback) scrollback.removeAt(0)
    }
}
