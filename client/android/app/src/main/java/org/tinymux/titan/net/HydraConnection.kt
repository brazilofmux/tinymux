package org.tinymux.titan.net

import io.grpc.ManagedChannel
import io.grpc.ManagedChannelBuilder
import io.grpc.Metadata
import io.grpc.stub.AbstractStub
import hydra.HydraServiceGrpcKt.HydraServiceCoroutineStub
import hydra.Hydra.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.flow

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
                // Create gRPC channel
                val ch = ManagedChannelBuilder
                    .forAddress(host, port)
                    .usePlaintext()
                    .build()
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

                // Open bidi GameSession stream
                val requests = flow {
                    while (connected) {
                        delay(30000)
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
        CoroutineScope(Dispatchers.IO).launch {
            try {
                val ch = channel ?: return@launch
                val stub = HydraServiceCoroutineStub(ch).withAuth()
                stub.sendInput(
                    InputRequest.newBuilder()
                        .setSessionId(sessionId)
                        .setLine(text)
                        .build()
                )
            } catch (_: Exception) {}
        }
    }

    private fun addScrollback(line: String) {
        scrollback.add(line)
        while (scrollback.size > maxScrollback) scrollback.removeAt(0)
    }
}
