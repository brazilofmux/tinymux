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
    @Volatile private var intentionalDisconnect = false
    val scrollback = mutableListOf<String>()
    private val maxScrollback = 20000

    private companion object {
        const val MAX_RECONNECT_ATTEMPTS = 5
        const val RECONNECT_DELAY_MS = 3000L
    }

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
                    dispatchServerMessage(msg, mainDispatcher)
                }
            } catch (_: Exception) {
                // Stream ended or error
            }

            // Stream ended — attempt reconnect if not intentional
            if (!intentionalDisconnect && sessionId.isNotEmpty() && channel != null) {
                connected = false
                val ch = channel ?: return@launch
                val stub = HydraServiceCoroutineStub(ch).withAuth()

                for (attempt in 1..MAX_RECONNECT_ATTEMPTS) {
                    val msg = "[Hydra] Stream lost, reconnecting (attempt $attempt)..."
                    addScrollback(msg)
                    launch(mainDispatcher) { onLine?.invoke(msg) }

                    delay(RECONNECT_DELAY_MS)

                    if (intentionalDisconnect) break

                    try {
                        val newInput = Channel<ClientMessage>(Channel.BUFFERED)
                        val newPings = flow {
                            while (true) {
                                delay(60000)
                                emit(
                                    ClientMessage.newBuilder()
                                        .setPing(PingMessage.newBuilder()
                                            .setClientTimestamp(System.currentTimeMillis())
                                            .build())
                                        .build()
                                )
                            }
                        }
                        @OptIn(kotlinx.coroutines.FlowPreview::class)
                        val newRequests = merge(newInput.consumeAsFlow(), newPings)
                        val newResponses = stub.gameSession(newRequests)

                        // Reconnect succeeded — replace input channel
                        // Close old inputChannel and swap
                        inputChannel.close()
                        // We can't reassign val, so we use the new channel for this stream
                        connected = true
                        val reconMsg = "[Hydra] Reconnected (attempt $attempt)"
                        addScrollback(reconMsg)
                        launch(mainDispatcher) { onLine?.invoke(reconMsg) }

                        newResponses.collect { msg2 ->
                            dispatchServerMessage(msg2, mainDispatcher)
                        }

                        // Stream broke again
                        if (!intentionalDisconnect) {
                            connected = false
                            continue
                        }
                        break
                    } catch (_: Exception) {
                        connected = false
                        continue
                    }
                }

                if (!intentionalDisconnect && !connected) {
                    val failMsg = "[Hydra] Reconnect failed after $MAX_RECONNECT_ATTEMPTS attempts"
                    addScrollback(failMsg)
                    launch(mainDispatcher) { onLine?.invoke(failMsg) }
                }
            }

            connected = false
            channel?.shutdownNow()
            channel = null
            if (!intentionalDisconnect) {
                launch(mainDispatcher) { onDisconnect?.invoke() }
            }
        }.also { sessionJob = it }
    }

    fun disconnect() {
        intentionalDisconnect = true
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

    private fun CoroutineScope.dispatchServerMessage(
        msg: ServerMessage, mainDispatcher: CoroutineDispatcher
    ) {
        val text = when (msg.payloadCase) {
            ServerMessage.PayloadCase.GAME_OUTPUT -> msg.gameOutput.text
            ServerMessage.PayloadCase.GMCP -> "[GMCP ${msg.gmcp.`package`}] ${msg.gmcp.json}"
            ServerMessage.PayloadCase.NOTICE -> "[Hydra] ${msg.notice.text}"
            ServerMessage.PayloadCase.LINK_EVENT -> {
                val ev = msg.linkEvent
                "[Hydra] Link ${ev.linkNumber} (${ev.gameName}): ${ev.newState.name}"
            }
            ServerMessage.PayloadCase.PONG -> null
            else -> null
        }
        if (text != null) {
            addScrollback(text)
            launch(mainDispatcher) { onLine?.invoke(text) }
        }
    }

    private fun addScrollback(line: String) {
        scrollback.add(line)
        while (scrollback.size > maxScrollback) scrollback.removeAt(0)
    }
}
