package org.tinymux.titan.ui

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.ClickableText
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.key.*
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.launch
import kotlinx.coroutines.CompletableDeferred
import org.tinymux.titan.data.AppSettings
import org.tinymux.titan.data.Hook
import org.tinymux.titan.data.HookRepository
import org.tinymux.titan.data.SessionLogger
import org.tinymux.titan.data.TimerEngine
import org.tinymux.titan.data.Trigger
import org.tinymux.titan.data.TriggerEngine
import org.tinymux.titan.data.TriggerRepository
import org.tinymux.titan.data.World
import org.tinymux.titan.data.WorldRepository
import android.content.Intent
import org.tinymux.titan.data.SpawnConfig
import org.tinymux.titan.data.SpawnRepository
import org.tinymux.titan.net.AnsiParser
import org.tinymux.titan.net.CertInfo
import org.tinymux.titan.net.MudConnection
import org.tinymux.titan.net.TofuCertStore
import org.tinymux.titan.service.ConnectionService

import androidx.compose.runtime.snapshots.SnapshotStateList

class WorldTab(
    val name: String,
    var connection: MudConnection? = null,
    val lines: SnapshotStateList<AnnotatedString> = mutableStateListOf(),
    val history: MutableList<String> = mutableListOf(),
    var hasActivity: Boolean = false,
    var disconnected: Boolean = false,
    val spawnLines: MutableMap<String, SnapshotStateList<AnnotatedString>> = mutableMapOf(),
    var activeSpawn: String = "",  // "" = main, otherwise spawn path
)

@Composable
fun TitanApp() {
    val context = LocalContext.current
    val worldRepo = remember { WorldRepository(context) }
    val triggerRepo = remember { TriggerRepository(context) }
    val spawnRepo = remember { SpawnRepository(context) }
    val hookRepo = remember { HookRepository(context) }
    val triggerEngine = remember { TriggerEngine() }
    val sessionLogger = remember { SessionLogger(context) }
    var logActive by remember { mutableStateOf(false) }
    val appSettings = remember { AppSettings(context) }
    val certStore = remember { TofuCertStore(context) }
    var pendingCert by remember { mutableStateOf<Pair<CertInfo, CompletableDeferred<Boolean>>?>(null) }

    val scope = rememberCoroutineScope()
    val timerEngine = remember { TimerEngine(scope) }

    // Load triggers on startup and whenever they change
    var triggerVersion by remember { mutableIntStateOf(0) }
    LaunchedEffect(triggerVersion) {
        triggerEngine.load(triggerRepo.load())
    }

    val tabs = remember { mutableStateListOf(WorldTab("(System)")) }

    var activeTab by remember { mutableIntStateOf(0) }
    var inputText by remember { mutableStateOf("") }
    var historyPos by remember { mutableIntStateOf(-1) }
    var savedInput by remember { mutableStateOf("") }
    val listState = rememberLazyListState()
    val focusRequester = remember { FocusRequester() }
    var showConnectDialog by remember { mutableStateOf(false) }
    var showWorldManager by remember { mutableStateOf(false) }
    var showTriggerManager by remember { mutableStateOf(false) }
    var showSettings by remember { mutableStateOf(false) }
    var showFindBar by remember { mutableStateOf(false) }
    var findQuery by remember { mutableStateOf("") }
    var findMatches by remember { mutableStateOf(listOf<Int>()) }
    var findPos by remember { mutableIntStateOf(-1) }

    val config = LocalConfiguration.current
    val isLandscape = config.screenWidthDp > config.screenHeightDp
    var settingsVersion by remember { mutableIntStateOf(0) }
    val fontSize = if (isLandscape) appSettings.fontSizeLandscape.sp else appSettings.fontSize.sp
    val monoStyle = TextStyle(fontFamily = FontFamily.Monospace, fontSize = fontSize, color = Color(0xFFC0C0C0))
    val scrollbackLimit = appSettings.scrollbackLines

    fun currentTab() = tabs.getOrNull(activeTab)

    fun activeLines(): List<AnnotatedString> {
        val tab = currentTab() ?: return emptyList()
        return if (tab.activeSpawn.isEmpty()) tab.lines
        else tab.spawnLines[tab.activeSpawn] ?: emptyList()
    }

    fun updateService() {
        val count = tabs.count { it.connection?.connected == true }
        if (count > 0) {
            val intent = Intent(context, ConnectionService::class.java).apply {
                action = ConnectionService.ACTION_UPDATE
                putExtra(ConnectionService.EXTRA_COUNT, count)
            }
            context.startForegroundService(intent)
        } else {
            val intent = Intent(context, ConnectionService::class.java).apply {
                action = ConnectionService.ACTION_STOP
            }
            try { context.startService(intent) } catch (_: Exception) {}
        }
    }

    // Wire timer fire callback — sends command to active tab's connection
    timerEngine.onFire = { name, command ->
        val tab = tabs.getOrNull(activeTab)
        val conn = tab?.connection
        if (conn != null && conn.connected) {
            conn.sendLine(command)
        }
    }

    fun appendLine(tabIndex: Int, line: String) {
        val parsed = AnsiParser.parse(line)
        tabs.getOrNull(tabIndex)?.let { tab ->
            // Main spawn always gets the line
            tab.lines.add(parsed)
            while (tab.lines.size > scrollbackLimit) tab.lines.removeAt(0)

            // Route to matching spawns
            val plain = AnsiParser.stripAnsi(line)
            for (spawn in spawnRepo.load()) {
                if (spawn.matches(plain)) {
                    val spawnBuf = tab.spawnLines.getOrPut(spawn.path) { mutableStateListOf() }
                    val display = if (spawn.prefix.isNotBlank()) {
                        AnsiParser.parse("${spawn.prefix}$line")
                    } else parsed
                    spawnBuf.add(display)
                    while (spawnBuf.size > spawn.maxLines) spawnBuf.removeAt(0)
                }
            }
            if (tabIndex != activeTab) {
                if (!tab.hasActivity) {
                    // First activity on background tab — fire ACTIVITY hooks
                    hookRepo.fireEvent("ACTIVITY")
                }
                tab.hasActivity = true
            }
        }
        if (sessionLogger.active && tabIndex == activeTab) {
            sessionLogger.writeLine(AnsiParser.stripAnsi(line))
        }
    }

    fun processServerLine(tabIndex: Int, line: String) {
        val result = triggerEngine.check(AnsiParser.stripAnsi(line))
        if (result.gagged) return
        val display = result.hiliteLine ?: line
        appendLine(tabIndex, display)
        // Execute trigger commands
        val tab = tabs.getOrNull(tabIndex) ?: return
        val conn = tab.connection
        if (conn != null && conn.connected) {
            for (cmd in result.commands) conn.sendLine(cmd)
        }
    }

    fun connectWorld(name: String, host: String, port: Int, ssl: Boolean, loginCommands: List<String> = emptyList()) {
        val tab = WorldTab(name)
        tabs.add(tab)
        val tabIndex = tabs.size - 1
        activeTab = tabIndex

        val conn = MudConnection(name, host, port, ssl, certStore)
        tab.connection = conn
        conn.onCertVerify = { certInfo ->
            val deferred = CompletableDeferred<Boolean>()
            pendingCert = certInfo to deferred
            deferred.await()
        }
        conn.onLine = { line -> processServerLine(tabIndex, line) }
        conn.onConnect = {
            appendLine(tabIndex, "% Connected to $host:$port")
            tab.disconnected = false
            for (cmd in hookRepo.fireEvent("CONNECT")) conn.sendLine(cmd)
            for (cmd in loginCommands) conn.sendLine(cmd)
            updateService()
        }
        conn.onDisconnect = {
            appendLine(tabIndex, "% Connection lost.")
            tab.disconnected = true
            timerEngine.cancelAll()
            hookRepo.fireEvent("DISCONNECT").forEach { cmd ->
                appendLine(tabIndex, "% [hook] $cmd")
            }
            updateService()
        }
        appendLine(tabIndex, "% Connecting to $host:$port${if (ssl) " (ssl)" else ""}...")
        // Start foreground service before connecting
        val startIntent = Intent(context, ConnectionService::class.java).apply {
            action = ConnectionService.ACTION_START
        }
        context.startForegroundService(startIntent)
        conn.connect(scope)
    }

    fun handleCommand(cmd: String, args: String): Boolean {
        val tab = currentTab()
        val idx = activeTab
        when (cmd) {
            "connect" -> {
                // /connect host [port] [ssl]
                val parts = args.split("\\s+".toRegex())
                if (parts.isEmpty() || parts[0].isBlank()) {
                    showConnectDialog = true
                } else {
                    val host = parts[0]
                    val port = parts.getOrNull(1)?.toIntOrNull() ?: 4201
                    val ssl = parts.any { it.equals("ssl", ignoreCase = true) || it.equals("tls", ignoreCase = true) }
                    connectWorld("$host:$port", host, port, ssl)
                }
            }
            "dc", "disconnect" -> {
                if (activeTab > 0) {
                    tabs.getOrNull(activeTab)?.connection?.disconnect()
                    tabs.removeAt(activeTab)
                    activeTab = (activeTab - 1).coerceAtLeast(0)
                    updateService()
                } else {
                    appendLine(idx, "% No active connection.")
                }
            }
            "worlds" -> showWorldManager = true
            "triggers", "trig" -> showTriggerManager = true
            "def" -> {
                // /def name pattern = body
                val eqPos = args.indexOf('=')
                if (eqPos < 0) {
                    appendLine(idx, "% Usage: /def <name> <pattern> = <action>")
                } else {
                    val before = args.substring(0, eqPos).trim().split("\\s+".toRegex(), limit = 2)
                    val body = args.substring(eqPos + 1).trim()
                    if (before.size < 2 || before[0].isBlank()) {
                        appendLine(idx, "% Usage: /def <name> <pattern> = <action>")
                    } else {
                        val name = before[0]
                        val pattern = before[1]
                        try {
                            Regex(pattern)
                            triggerRepo.add(Trigger(name = name, pattern = pattern, body = body))
                            triggerVersion++
                            appendLine(idx, "% Trigger '$name' defined: /$pattern/")
                        } catch (e: Exception) {
                            appendLine(idx, "% Bad pattern: ${e.message}")
                        }
                    }
                }
            }
            "undef" -> {
                val name = args.trim()
                if (name.isBlank()) {
                    appendLine(idx, "% Usage: /undef <name>")
                } else {
                    triggerRepo.remove(name)
                    triggerVersion++
                    appendLine(idx, "% Trigger '$name' removed.")
                }
            }
            "find" -> {
                showFindBar = true
                val query = args.trim()
                if (query.isNotBlank()) findQuery = query
            }
            "log" -> {
                if (sessionLogger.active) {
                    val file = sessionLogger.currentFile()
                    sessionLogger.stop()
                    logActive = false
                    appendLine(idx, "% Logging stopped. File: ${file?.name}")
                } else {
                    val worldName = tab?.name ?: "system"
                    val filename = args.trim().ifBlank { null }
                    val file = sessionLogger.start(worldName, filename)
                    logActive = true
                    appendLine(idx, "% Logging to: ${file.name}")
                    appendLine(idx, "% Log directory: ${sessionLogger.logDir.absolutePath}")
                }
            }
            "repeat" -> {
                // /repeat <name> <seconds> [-n shots] <command>
                val parts = args.trim().split("\\s+".toRegex(), limit = 4)
                if (parts.size < 3) {
                    appendLine(idx, "% Usage: /repeat <name> <seconds> <command>")
                } else {
                    val tName = parts[0]
                    val seconds = parts[1].toDoubleOrNull()
                    if (seconds == null || seconds <= 0) {
                        appendLine(idx, "% Invalid interval: ${parts[1]}")
                    } else {
                        val command = parts.drop(2).joinToString(" ")
                        timerEngine.add(tName, command, (seconds * 1000).toLong())
                        appendLine(idx, "% Timer '$tName' set: every ${seconds}s -> $command")
                    }
                }
            }
            "killtimer", "cancel" -> {
                val tName = args.trim()
                if (tName.isBlank()) {
                    appendLine(idx, "% Usage: /killtimer <name>")
                } else if (timerEngine.remove(tName)) {
                    appendLine(idx, "% Timer '$tName' cancelled.")
                } else {
                    appendLine(idx, "% No timer named '$tName'.")
                }
            }
            "timers", "listtimers" -> {
                val list = timerEngine.list()
                if (list.isEmpty()) {
                    appendLine(idx, "% No active timers.")
                } else {
                    appendLine(idx, "% Active timers:")
                    list.forEach { t ->
                        val shots = if (t.shotsRemaining < 0) "inf" else "${t.shotsRemaining}"
                        appendLine(idx, "%   ${t.name}: every ${t.intervalMs / 1000.0}s, shots=$shots -> ${t.command}")
                    }
                }
            }
            "hook" -> {
                // /hook <name> <event> = <command>
                val eqPos = args.indexOf('=')
                if (eqPos < 0) {
                    appendLine(idx, "% Usage: /hook <name> <event> = <command>")
                    appendLine(idx, "% Events: ${Hook.EVENTS.joinToString(", ")}")
                } else {
                    val before = args.substring(0, eqPos).trim().split("\\s+".toRegex(), limit = 2)
                    val body = args.substring(eqPos + 1).trim()
                    if (before.size < 2 || before[0].isBlank()) {
                        appendLine(idx, "% Usage: /hook <name> <event> = <command>")
                    } else {
                        val hName = before[0]
                        val event = before[1].uppercase()
                        if (event !in Hook.EVENTS) {
                            appendLine(idx, "% Unknown event '$event'. Valid: ${Hook.EVENTS.joinToString(", ")}")
                        } else {
                            hookRepo.add(Hook(name = hName, event = event, body = body))
                            appendLine(idx, "% Hook '$hName' on $event -> $body")
                        }
                    }
                }
            }
            "unhook" -> {
                val hName = args.trim()
                if (hName.isBlank()) {
                    appendLine(idx, "% Usage: /unhook <name>")
                } else {
                    hookRepo.remove(hName)
                    appendLine(idx, "% Hook '$hName' removed.")
                }
            }
            "hooks", "listhooks" -> {
                val list = hookRepo.load()
                if (list.isEmpty()) {
                    appendLine(idx, "% No hooks defined.")
                } else {
                    appendLine(idx, "% Hooks:")
                    list.forEach { h ->
                        val state = if (h.enabled) "on" else "off"
                        appendLine(idx, "%   ${h.name}: ${h.event} [$state] -> ${h.body}")
                    }
                }
            }
            "spawn" -> {
                val parts = args.trim().split("\\s+".toRegex(), limit = 3)
                when (parts.getOrNull(0)?.lowercase()) {
                    "add" -> {
                        if (parts.size < 3) {
                            appendLine(idx, "% Usage: /spawn add <name> <pattern>")
                        } else {
                            val sName = parts[1]
                            val pattern = parts[2]
                            try {
                                Regex(pattern)
                                spawnRepo.add(SpawnConfig(name = sName, path = sName.lowercase(), patterns = listOf(pattern)))
                                appendLine(idx, "% Spawn '$sName' added: /$pattern/")
                            } catch (e: Exception) {
                                appendLine(idx, "% Bad pattern: ${e.message}")
                            }
                        }
                    }
                    "remove", "del" -> {
                        val sName = parts.getOrNull(1) ?: ""
                        if (sName.isBlank()) {
                            appendLine(idx, "% Usage: /spawn remove <name>")
                        } else {
                            spawnRepo.remove(sName.lowercase())
                            appendLine(idx, "% Spawn '$sName' removed.")
                        }
                    }
                    "list", null -> {
                        val list = spawnRepo.load()
                        if (list.isEmpty()) {
                            appendLine(idx, "% No spawns defined. Use /spawn add <name> <pattern>")
                        } else {
                            appendLine(idx, "% Spawns:")
                            list.forEach { s ->
                                appendLine(idx, "%   ${s.name} (${s.path}): ${s.patterns.joinToString(", ") { "/$it/" }}")
                            }
                        }
                    }
                    "focus", "fg" -> {
                        val sName = parts.getOrNull(1) ?: ""
                        if (sName.isBlank() || sName.equals("main", ignoreCase = true)) {
                            tab?.activeSpawn = ""
                        } else {
                            tab?.activeSpawn = sName.lowercase()
                        }
                    }
                    else -> appendLine(idx, "% Usage: /spawn [add|remove|list|focus] ...")
                }
            }
            "clear" -> tab?.lines?.clear()
            "help" -> {
                appendLine(idx, "% Commands:")
                appendLine(idx, "%   /connect <host> [port] [ssl]  - Connect to a world")
                appendLine(idx, "%   /disconnect, /dc              - Close current connection")
                appendLine(idx, "%   /worlds                       - Open World Manager")
                appendLine(idx, "%   /triggers                     - Open Trigger Manager")
                appendLine(idx, "%   /def <name> <pattern> = <cmd> - Define a trigger")
                appendLine(idx, "%   /undef <name>                 - Remove a trigger")
                appendLine(idx, "%   /find <text>                  - Search scrollback")
                appendLine(idx, "%   /log [filename]               - Toggle session logging")
                appendLine(idx, "%   /repeat <name> <sec> <cmd>   - Create repeating timer")
                appendLine(idx, "%   /killtimer <name>             - Cancel a timer")
                appendLine(idx, "%   /timers                       - List active timers")
                appendLine(idx, "%   /hook <name> <event> = <cmd>  - Define event hook")
                appendLine(idx, "%   /unhook <name>                - Remove a hook")
                appendLine(idx, "%   /hooks                        - List hooks")
                appendLine(idx, "%   /spawn add <name> <pattern>  - Add output spawn")
                appendLine(idx, "%   /spawn remove <name>          - Remove spawn")
                appendLine(idx, "%   /spawn list                   - List spawns")
                appendLine(idx, "%   /spawn focus <name|main>      - Switch spawn view")
                appendLine(idx, "%   /clear                        - Clear scrollback")
                appendLine(idx, "%   /help                         - Show this help")
            }
            else -> {
                appendLine(idx, "% Unknown command: /$cmd  (try /help)")
                return false
            }
        }
        return true
    }

    fun handleInput(text: String) {
        if (text.isBlank()) return
        val tab = currentTab() ?: return

        // Save to history
        tab.history.add(0, text)
        if (tab.history.size > 500) tab.history.removeLast()
        historyPos = -1

        // Slash commands
        if (text.startsWith("/")) {
            val trimmed = text.substring(1)
            val spacePos = trimmed.indexOf(' ')
            val cmd = if (spacePos < 0) trimmed else trimmed.substring(0, spacePos)
            val args = if (spacePos < 0) "" else trimmed.substring(spacePos + 1)
            handleCommand(cmd.lowercase(), args)
            inputText = ""
            return
        }

        val conn = tab.connection
        if (conn != null && conn.connected) {
            conn.sendLine(text)
            if (!conn.telnet.remoteEcho) {
                appendLine(activeTab, "> $text")
            }
        } else {
            appendLine(activeTab, "> $text")
        }
        inputText = ""

        // Auto-scroll
        scope.launch {
            val lines = currentTab()?.lines ?: return@launch
            if (lines.isNotEmpty()) listState.animateScrollToItem(lines.size - 1)
        }
    }

    // Add welcome text to system tab
    LaunchedEffect(Unit) {
        appendLine(0, "Titan for Android")
        appendLine(0, "Tap Connect or Worlds to get started.")
        focusRequester.requestFocus()
    }

    // Keep screen on setting
    val activity = context as? android.app.Activity
    DisposableEffect(appSettings.keepScreenOn) {
        if (appSettings.keepScreenOn) {
            activity?.window?.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            activity?.window?.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        onDispose {
            activity?.window?.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    // Auto-scroll when new lines arrive
    val lineCount = currentTab()?.lines?.size ?: 0
    LaunchedEffect(lineCount) {
        if (lineCount > 0) listState.animateScrollToItem(lineCount - 1)
    }

    Column(modifier = Modifier
        .fillMaxSize()
        .background(Color.Black)
        .statusBarsPadding()
        .navigationBarsPadding()
        .imePadding()
    ) {
        // Toolbar — compact in landscape
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF252525))
                .padding(horizontal = 4.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            ToolbarButton("Connect") { showConnectDialog = true }
            ToolbarButton("Worlds") { showWorldManager = true }
            ToolbarButton("Trig") { showTriggerManager = true }
            ToolbarButton("Find") { showFindBar = !showFindBar }
            ToolbarButton("Cfg") { showSettings = true }
            ToolbarButton("DC") {
                if (activeTab > 0) {
                    tabs.getOrNull(activeTab)?.connection?.disconnect()
                    tabs.removeAt(activeTab)
                    activeTab = (activeTab - 1).coerceAtLeast(0)
                    updateService()
                }
            }
            if (!isLandscape) {
                Spacer(Modifier.weight(1f))
                ToolbarButton("Clear") { currentTab()?.lines?.clear() }
            }
        }

        // Tab bar
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState())
                .background(Color(0xFF303030))
                .height(36.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            tabs.forEachIndexed { index, tab ->
                val isActive = index == activeTab
                Row(
                    modifier = Modifier
                        .clickable {
                            activeTab = index
                            tab.hasActivity = false
                        }
                        .background(if (isActive) Color.Black else Color.Transparent)
                        .padding(start = 12.dp, end = if (index > 0) 4.dp else 12.dp, top = 6.dp, bottom = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    if (tab.hasActivity && !isActive) {
                        Box(Modifier
                            .size(6.dp)
                            .background(Color(0xFFFFC800), shape = androidx.compose.foundation.shape.CircleShape))
                        Spacer(Modifier.width(6.dp))
                    }
                    Text(
                        tab.name,
                        fontSize = 12.sp,
                        color = if (tab.disconnected) Color(0xFF806060)
                                else if (isActive) Color.White
                                else Color(0xFFA0A0A0)
                    )
                    // Close button (not on System tab)
                    if (index > 0) {
                        Spacer(Modifier.width(4.dp))
                        Text(
                            "\u2715",
                            fontSize = 10.sp,
                            color = Color(0xFF808080),
                            modifier = Modifier.clickable {
                                tab.connection?.disconnect()
                                tabs.removeAt(index)
                                if (activeTab >= tabs.size) activeTab = tabs.size - 1
                                else if (activeTab > index) activeTab--
                                updateService()
                            }
                        )
                    }
                }
                // Separator
                Box(Modifier.width(1.dp).height(20.dp).background(Color(0xFF505050)))
            }
        }

        // Spawn selector bar (only when spawns are configured)
        val spawns = spawnRepo.load()
        if (spawns.isNotEmpty() && activeTab > 0) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState())
                    .background(Color(0xFF1A2A1A))
                    .height(28.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                val tab = currentTab()
                // Main button
                val isMain = tab?.activeSpawn?.isEmpty() == true
                Text(
                    "Main",
                    fontSize = 11.sp,
                    color = if (isMain) Color.White else Color(0xFF80A080),
                    modifier = Modifier
                        .clickable { tab?.activeSpawn = "" }
                        .background(if (isMain) Color(0xFF2A4A2A) else Color.Transparent)
                        .padding(horizontal = 10.dp, vertical = 4.dp)
                )
                spawns.forEach { spawn ->
                    val isActive = tab?.activeSpawn == spawn.path
                    val hasContent = tab?.spawnLines?.get(spawn.path)?.isNotEmpty() == true
                    Text(
                        spawn.name,
                        fontSize = 11.sp,
                        color = if (isActive) Color.White
                                else if (hasContent) Color(0xFFA0C0A0)
                                else Color(0xFF607060),
                        modifier = Modifier
                            .clickable { tab?.activeSpawn = spawn.path }
                            .background(if (isActive) Color(0xFF2A4A2A) else Color.Transparent)
                            .padding(horizontal = 10.dp, vertical = 4.dp)
                    )
                }
            }
        }

        // Find bar
        if (showFindBar) {
            val findFocusRequester = remember { FocusRequester() }

            fun updateMatches(query: String) {
                if (query.isBlank()) {
                    findMatches = emptyList(); findPos = -1; return
                }
                val lines = currentTab()?.lines ?: emptyList()
                val matches = mutableListOf<Int>()
                for (i in lines.indices) {
                    if (lines[i].text.contains(query, ignoreCase = true)) matches.add(i)
                }
                findMatches = matches
                findPos = if (matches.isNotEmpty()) matches.size - 1 else -1
                if (findPos >= 0) scope.launch { listState.animateScrollToItem(matches[findPos]) }
            }

            LaunchedEffect(Unit) { findFocusRequester.requestFocus() }

            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF1A1A2E))
                    .padding(horizontal = 4.dp, vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                BasicTextField(
                    value = findQuery,
                    onValueChange = { findQuery = it; updateMatches(it) },
                    modifier = Modifier
                        .weight(1f)
                        .focusRequester(findFocusRequester),
                    textStyle = monoStyle.copy(color = Color.White, fontSize = 12.sp),
                    cursorBrush = SolidColor(Color.White),
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                    keyboardActions = KeyboardActions(onSearch = { updateMatches(findQuery) }),
                )
                Text(
                    if (findMatches.isEmpty() && findQuery.isNotBlank()) "0/0"
                    else if (findMatches.isNotEmpty()) "${findPos + 1}/${findMatches.size}"
                    else "",
                    fontSize = 11.sp, color = Color(0xFFA0A0A0),
                    modifier = Modifier.padding(horizontal = 4.dp),
                )
                ToolbarButton("\u25B2") {
                    if (findMatches.isNotEmpty()) {
                        findPos = if (findPos > 0) findPos - 1 else findMatches.size - 1
                        scope.launch { listState.animateScrollToItem(findMatches[findPos]) }
                    }
                }
                ToolbarButton("\u25BC") {
                    if (findMatches.isNotEmpty()) {
                        findPos = if (findPos < findMatches.size - 1) findPos + 1 else 0
                        scope.launch { listState.animateScrollToItem(findMatches[findPos]) }
                    }
                }
                ToolbarButton("\u2715") {
                    showFindBar = false; findQuery = ""; findMatches = emptyList(); findPos = -1
                }
            }
        }

        // Output pane
        val uriHandler = LocalUriHandler.current
        val clipboardManager = LocalClipboardManager.current
        SelectionContainer {
            LazyColumn(
                state = listState,
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .padding(horizontal = 4.dp),
            ) {
                val lines = activeLines()
                items(lines) { line ->
                    @Suppress("DEPRECATION")
                    ClickableText(
                        text = line,
                        style = monoStyle,
                        softWrap = true,
                        modifier = Modifier.fillMaxWidth(),
                        onClick = { offset ->
                            line.getStringAnnotations("URL", offset, offset).firstOrNull()?.let {
                                try { uriHandler.openUri(it.item) } catch (_: Exception) {}
                            }
                        }
                    )
                }
            }
        }

        // Input bar
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF202020))
                .padding(4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            BasicTextField(
                value = inputText,
                onValueChange = { inputText = it },
                modifier = Modifier
                    .weight(1f)
                    .focusRequester(focusRequester)
                    .onPreviewKeyEvent { event ->
                        if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                        val tab = currentTab()
                        when {
                            // Up arrow — history back
                            event.key == Key.DirectionUp -> {
                                val hist = tab?.history ?: return@onPreviewKeyEvent true
                                if (hist.isNotEmpty()) {
                                    if (historyPos < 0) savedInput = inputText
                                    val next = (historyPos + 1).coerceAtMost(hist.size - 1)
                                    historyPos = next
                                    inputText = hist[next]
                                }
                                true
                            }
                            // Down arrow — history forward
                            event.key == Key.DirectionDown -> {
                                val hist = tab?.history ?: return@onPreviewKeyEvent true
                                if (historyPos > 0) {
                                    historyPos--
                                    inputText = hist[historyPos]
                                } else if (historyPos == 0) {
                                    historyPos = -1
                                    inputText = savedInput
                                }
                                true
                            }
                            // Ctrl+F — find
                            event.isCtrlPressed && event.key == Key.F -> {
                                showFindBar = !showFindBar; true
                            }
                            // Ctrl+L — clear
                            event.isCtrlPressed && event.key == Key.L -> {
                                tab?.lines?.clear(); true
                            }
                            // Escape — close find bar
                            event.key == Key.Escape -> {
                                if (showFindBar) {
                                    showFindBar = false; findQuery = ""
                                    findMatches = emptyList(); findPos = -1
                                    true
                                } else false
                            }
                            // Page Up — scroll back
                            event.key == Key.PageUp -> {
                                scope.launch {
                                    val first = listState.firstVisibleItemIndex
                                    listState.animateScrollToItem((first - 20).coerceAtLeast(0))
                                }
                                true
                            }
                            // Page Down — scroll forward
                            event.key == Key.PageDown -> {
                                scope.launch {
                                    val first = listState.firstVisibleItemIndex
                                    val max = (tab?.lines?.size ?: 1) - 1
                                    listState.animateScrollToItem((first + 20).coerceAtMost(max))
                                }
                                true
                            }
                            else -> false
                        }
                    },
                textStyle = monoStyle.copy(color = Color.White),
                cursorBrush = SolidColor(Color.White),
                singleLine = true,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                keyboardActions = KeyboardActions(onSend = { handleInput(inputText) }),
            )
            Spacer(Modifier.width(4.dp))
            ToolbarButton("Send") { handleInput(inputText) }
        }

        // Status bar — hidden in landscape when keyboard is up to save space
        if (!isLandscape || lineCount > 0) {
            val tab = currentTab()
            val status = buildString {
                append(tab?.name ?: "(no connection)")
                tab?.connection?.let { conn ->
                    if (conn.useSsl) append(" [ssl]")
                    if (!conn.connected) append(" (disconnected)")
                }
                if (logActive) append(" [log]")
            }
            Text(
                text = status,
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF000080))
                    .padding(horizontal = 8.dp, vertical = 2.dp),
                color = Color.White,
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
            )
        }
    }

    // Connect dialog
    if (showConnectDialog) {
        ConnectDialog(
            worldRepo = worldRepo,
            settings = appSettings,
            onConnect = { host, port, ssl, saveName ->
                showConnectDialog = false
                val name = saveName.ifBlank { "$host:$port" }
                if (saveName.isNotBlank()) {
                    worldRepo.add(World(name = saveName, host = host, port = port, ssl = ssl))
                }
                connectWorld(name, host, port, ssl)
            },
            onDismiss = { showConnectDialog = false }
        )
    }

    // World Manager dialog
    if (showWorldManager) {
        WorldManagerDialog(
            worldRepo = worldRepo,
            onConnect = { world ->
                showWorldManager = false
                connectWorld(world.name, world.host, world.port, world.ssl, world.loginCommands)
            },
            onDismiss = { showWorldManager = false }
        )
    }

    // Trigger Manager dialog
    if (showTriggerManager) {
        TriggerManagerDialog(
            triggerRepo = triggerRepo,
            onChanged = { triggerVersion++ },
            onDismiss = { showTriggerManager = false }
        )
    }

    // TLS Certificate verification dialog
    pendingCert?.let { (info, deferred) ->
        CertVerifyDialog(
            certInfo = info,
            onAccept = { pendingCert = null; deferred.complete(true) },
            onReject = { pendingCert = null; deferred.complete(false) },
        )
    }

    // Settings dialog
    if (showSettings) {
        SettingsDialog(
            settings = appSettings,
            onDismiss = {
                showSettings = false
                settingsVersion++
            }
        )
    }
}

@Composable
fun ToolbarButton(text: String, onClick: () -> Unit) {
    TextButton(
        onClick = onClick,
        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
        colors = ButtonDefaults.textButtonColors(contentColor = Color(0xFFD0D0D0)),
        modifier = Modifier.height(28.dp)
    ) {
        Text(text, fontSize = 12.sp)
    }
}

@Composable
fun CertVerifyDialog(
    certInfo: CertInfo,
    onAccept: () -> Unit,
    onReject: () -> Unit,
) {
    val isChanged = certInfo.savedFingerprint != null
    AlertDialog(
        onDismissRequest = onReject,
        title = { Text(if (isChanged) "Certificate Changed!" else "Unknown Certificate") },
        text = {
            Column(
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.verticalScroll(rememberScrollState())
            ) {
                if (isChanged) {
                    Text(
                        "WARNING: The certificate for ${certInfo.host}:${certInfo.port} " +
                        "has changed since last connection. This could indicate a " +
                        "man-in-the-middle attack.",
                        color = Color(0xFFFF6666),
                    )
                } else {
                    Text("First connection to ${certInfo.host}:${certInfo.port} via SSL.")
                }
                Text("Subject: ${certInfo.subject}", fontSize = 12.sp, color = Color.Gray)
                Text("Issuer: ${certInfo.issuer}", fontSize = 12.sp, color = Color.Gray)
                Text("SHA-256:", fontSize = 12.sp, fontWeight = FontWeight.Bold)
                Text(certInfo.fingerprint, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                if (isChanged && certInfo.savedFingerprint != null) {
                    Text("Previous fingerprint:", fontSize = 12.sp, fontWeight = FontWeight.Bold)
                    Text(certInfo.savedFingerprint, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onAccept) {
                Text(if (isChanged) "Accept Anyway" else "Trust")
            }
        },
        dismissButton = {
            TextButton(onClick = onReject) { Text("Reject") }
        }
    )
}

@Composable
fun ConnectDialog(
    worldRepo: WorldRepository,
    settings: AppSettings,
    onConnect: (host: String, port: Int, ssl: Boolean, saveName: String) -> Unit,
    onDismiss: () -> Unit,
) {
    var host by remember { mutableStateOf("") }
    var port by remember { mutableStateOf("") }
    var ssl by remember { mutableStateOf(settings.defaultSsl) }
    var saveName by remember { mutableStateOf("") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Connect") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(value = host, onValueChange = { host = it },
                    label = { Text("Host") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(value = port,
                    onValueChange = { port = it.filter { c -> c.isDigit() } },
                    label = { Text("Port") },
                    placeholder = { Text("4201") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth())
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp)
                        .clickable { ssl = !ssl }
                ) {
                    Switch(checked = ssl, onCheckedChange = { ssl = it })
                    Spacer(Modifier.width(8.dp))
                    Text("SSL/TLS", style = MaterialTheme.typography.bodyLarge)
                }
                OutlinedTextField(value = saveName, onValueChange = { saveName = it },
                    label = { Text("Save as (optional)") },
                    placeholder = { Text("e.g. MyMUD") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth())
            }
        },
        confirmButton = {
            TextButton(onClick = {
                if (host.isNotBlank()) onConnect(host.trim(), port.trim().toIntOrNull() ?: settings.defaultPort, ssl, saveName.trim())
            }) { Text("Connect") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}

// ---------------------------------------------------------------------------
// World Manager
// ---------------------------------------------------------------------------

@Composable
fun WorldManagerDialog(
    worldRepo: WorldRepository,
    onConnect: (World) -> Unit,
    onDismiss: () -> Unit,
) {
    var worlds by remember { mutableStateOf(worldRepo.load()) }
    var editingWorld by remember { mutableStateOf<World?>(null) }
    var showAdd by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf<World?>(null) }

    fun refresh() { worlds = worldRepo.load() }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Worlds") },
        text = {
            Column(modifier = Modifier.fillMaxWidth()) {
                if (worlds.isEmpty()) {
                    Text(
                        "No saved worlds yet.",
                        color = Color.Gray,
                        modifier = Modifier.padding(vertical = 16.dp)
                    )
                } else {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(max = 320.dp)
                            .verticalScroll(rememberScrollState())
                    ) {
                        worlds.forEach { world ->
                            WorldRow(
                                world = world,
                                onConnect = { onConnect(world) },
                                onEdit = { editingWorld = world },
                                onDelete = { confirmDelete = world },
                            )
                            HorizontalDivider(color = Color(0xFF404040))
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { showAdd = true }) { Text("Add") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Close") }
        }
    )

    // Add world
    if (showAdd) {
        EditWorldDialog(
            title = "Add World",
            initial = null,
            onSave = { world ->
                worldRepo.add(world)
                refresh()
                showAdd = false
            },
            onDismiss = { showAdd = false }
        )
    }

    // Edit world
    editingWorld?.let { world ->
        EditWorldDialog(
            title = "Edit World",
            initial = world,
            onSave = { updated ->
                // If name changed, remove old entry
                if (updated.name != world.name) worldRepo.remove(world.name)
                worldRepo.add(updated)
                refresh()
                editingWorld = null
            },
            onDismiss = { editingWorld = null }
        )
    }

    // Confirm delete
    confirmDelete?.let { world ->
        AlertDialog(
            onDismissRequest = { confirmDelete = null },
            title = { Text("Delete World") },
            text = { Text("Remove \"${world.name}\"?") },
            confirmButton = {
                TextButton(onClick = {
                    worldRepo.remove(world.name)
                    refresh()
                    confirmDelete = null
                }) { Text("Delete", color = Color(0xFFFF6666)) }
            },
            dismissButton = {
                TextButton(onClick = { confirmDelete = null }) { Text("Cancel") }
            }
        )
    }
}

@Composable
private fun WorldRow(
    world: World,
    onConnect: () -> Unit,
    onEdit: () -> Unit,
    onDelete: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onConnect)
            .padding(vertical = 8.dp, horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                world.name,
                fontWeight = FontWeight.Bold,
                fontSize = 14.sp,
            )
            Text(
                buildString {
                    append("${world.host}:${world.port}")
                    if (world.ssl) append(" (ssl)")
                    if (world.character.isNotBlank()) append(" - ${world.character}")
                    if (world.loginCommands.isNotEmpty()) append(" [auto-login]")
                },
                fontSize = 12.sp,
                color = Color.Gray,
            )
        }
        TextButton(onClick = onEdit, contentPadding = PaddingValues(horizontal = 8.dp)) {
            Text("Edit", fontSize = 12.sp)
        }
        TextButton(onClick = onDelete, contentPadding = PaddingValues(horizontal = 8.dp)) {
            Text("Del", fontSize = 12.sp, color = Color(0xFFFF6666))
        }
    }
}

@Composable
fun EditWorldDialog(
    title: String,
    initial: World?,
    onSave: (World) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by remember { mutableStateOf(initial?.name ?: "") }
    var host by remember { mutableStateOf(initial?.host ?: "") }
    var port by remember { mutableStateOf(initial?.port?.toString() ?: "") }
    var ssl by remember { mutableStateOf(initial?.ssl ?: false) }
    var character by remember { mutableStateOf(initial?.character ?: "") }
    var notes by remember { mutableStateOf(initial?.notes ?: "") }
    var loginCmds by remember { mutableStateOf(initial?.loginCommands?.joinToString("\n") ?: "") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.verticalScroll(rememberScrollState())
            ) {
                OutlinedTextField(value = name, onValueChange = { name = it },
                    label = { Text("Name") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(value = host, onValueChange = { host = it },
                    label = { Text("Host") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(value = port,
                    onValueChange = { port = it.filter { c -> c.isDigit() } },
                    label = { Text("Port") },
                    placeholder = { Text("4201") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth())
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp)
                        .clickable { ssl = !ssl }
                ) {
                    Switch(checked = ssl, onCheckedChange = { ssl = it })
                    Spacer(Modifier.width(8.dp))
                    Text("SSL/TLS", style = MaterialTheme.typography.bodyLarge)
                }
                OutlinedTextField(value = character, onValueChange = { character = it },
                    label = { Text("Character (optional)") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(value = notes, onValueChange = { notes = it },
                    label = { Text("Notes (optional)") },
                    maxLines = 3,
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(value = loginCmds, onValueChange = { loginCmds = it },
                    label = { Text("Login commands (one per line)") },
                    placeholder = { Text("e.g. connect Player pass") },
                    maxLines = 5,
                    modifier = Modifier.fillMaxWidth())
            }
        },
        confirmButton = {
            TextButton(onClick = {
                if (name.isNotBlank() && host.isNotBlank()) {
                    onSave(World(
                        name = name.trim(),
                        host = host.trim(),
                        port = port.trim().toIntOrNull() ?: 4201,
                        ssl = ssl,
                        character = character.trim(),
                        notes = notes.trim(),
                        loginCommands = loginCmds.lines().map { it.trim() }.filter { it.isNotBlank() },
                    ))
                }
            }) { Text("Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}

// ---------------------------------------------------------------------------
// Trigger Manager
// ---------------------------------------------------------------------------

@Composable
fun TriggerManagerDialog(
    triggerRepo: TriggerRepository,
    onChanged: () -> Unit,
    onDismiss: () -> Unit,
) {
    var triggers by remember { mutableStateOf(triggerRepo.load()) }
    var editingTrigger by remember { mutableStateOf<Trigger?>(null) }
    var showAdd by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf<Trigger?>(null) }

    fun refresh() {
        triggers = triggerRepo.load()
        onChanged()
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Triggers") },
        text = {
            Column(modifier = Modifier.fillMaxWidth()) {
                if (triggers.isEmpty()) {
                    Text(
                        "No triggers defined yet.",
                        color = Color.Gray,
                        modifier = Modifier.padding(vertical = 16.dp)
                    )
                } else {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(max = 320.dp)
                            .verticalScroll(rememberScrollState())
                    ) {
                        triggers.forEach { trigger ->
                            TriggerRow(
                                trigger = trigger,
                                onToggle = {
                                    triggerRepo.add(trigger.copy(enabled = !trigger.enabled))
                                    refresh()
                                },
                                onEdit = { editingTrigger = trigger },
                                onDelete = { confirmDelete = trigger },
                            )
                            HorizontalDivider(color = Color(0xFF404040))
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { showAdd = true }) { Text("Add") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Close") }
        }
    )

    // Add trigger
    if (showAdd) {
        EditTriggerDialog(
            title = "Add Trigger",
            initial = null,
            onSave = { trigger ->
                triggerRepo.add(trigger)
                refresh()
                showAdd = false
            },
            onDismiss = { showAdd = false }
        )
    }

    // Edit trigger
    editingTrigger?.let { trigger ->
        EditTriggerDialog(
            title = "Edit Trigger",
            initial = trigger,
            onSave = { updated ->
                if (updated.name != trigger.name) triggerRepo.remove(trigger.name)
                triggerRepo.add(updated)
                refresh()
                editingTrigger = null
            },
            onDismiss = { editingTrigger = null }
        )
    }

    // Confirm delete
    confirmDelete?.let { trigger ->
        AlertDialog(
            onDismissRequest = { confirmDelete = null },
            title = { Text("Delete Trigger") },
            text = { Text("Remove \"${trigger.name}\"?") },
            confirmButton = {
                TextButton(onClick = {
                    triggerRepo.remove(trigger.name)
                    refresh()
                    confirmDelete = null
                }) { Text("Delete", color = Color(0xFFFF6666)) }
            },
            dismissButton = {
                TextButton(onClick = { confirmDelete = null }) { Text("Cancel") }
            }
        )
    }
}

@Composable
private fun TriggerRow(
    trigger: Trigger,
    onToggle: () -> Unit,
    onEdit: () -> Unit,
    onDelete: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp, horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                trigger.name,
                fontWeight = FontWeight.Bold,
                fontSize = 14.sp,
                color = if (trigger.enabled) Color.Unspecified else Color.Gray,
            )
            Text(
                buildString {
                    append("/${trigger.pattern}/")
                    val flags = mutableListOf<String>()
                    if (trigger.gag) flags.add("gag")
                    if (trigger.hilite) flags.add("hilite")
                    if (trigger.body.isNotBlank()) flags.add("cmd")
                    if (trigger.shots >= 0) flags.add("${trigger.shots} shots")
                    if (flags.isNotEmpty()) append(" [${flags.joinToString(", ")}]")
                },
                fontSize = 11.sp,
                color = Color.Gray,
            )
        }
        TextButton(onClick = onToggle, contentPadding = PaddingValues(horizontal = 4.dp)) {
            Text(if (trigger.enabled) "On" else "Off", fontSize = 11.sp,
                color = if (trigger.enabled) Color(0xFF66FF66) else Color.Gray)
        }
        TextButton(onClick = onEdit, contentPadding = PaddingValues(horizontal = 4.dp)) {
            Text("Edit", fontSize = 11.sp)
        }
        TextButton(onClick = onDelete, contentPadding = PaddingValues(horizontal = 4.dp)) {
            Text("Del", fontSize = 11.sp, color = Color(0xFFFF6666))
        }
    }
}

@Composable
fun EditTriggerDialog(
    title: String,
    initial: Trigger?,
    onSave: (Trigger) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by remember { mutableStateOf(initial?.name ?: "") }
    var pattern by remember { mutableStateOf(initial?.pattern ?: "") }
    var body by remember { mutableStateOf(initial?.body ?: "") }
    var priority by remember { mutableStateOf(initial?.priority?.toString() ?: "0") }
    var shots by remember { mutableStateOf(initial?.shots?.toString() ?: "-1") }
    var gag by remember { mutableStateOf(initial?.gag ?: false) }
    var hilite by remember { mutableStateOf(initial?.hilite ?: false) }
    var patternError by remember { mutableStateOf<String?>(null) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.verticalScroll(rememberScrollState())
            ) {
                OutlinedTextField(value = name, onValueChange = { name = it },
                    label = { Text("Name") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(
                    value = pattern,
                    onValueChange = {
                        pattern = it
                        patternError = try { Regex(it); null }
                        catch (e: Exception) { e.message }
                    },
                    label = { Text("Pattern (regex)") },
                    placeholder = { Text("e.g. tells you .*") },
                    singleLine = true,
                    isError = patternError != null,
                    supportingText = patternError?.let { { Text(it, color = Color(0xFFFF6666), fontSize = 10.sp) } },
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(value = body, onValueChange = { body = it },
                    label = { Text("Action (command to send)") },
                    placeholder = { Text("e.g. say Hello!") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    OutlinedTextField(value = priority,
                        onValueChange = { priority = it.filter { c -> c.isDigit() || c == '-' } },
                        label = { Text("Priority") },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Number),
                        modifier = Modifier.weight(1f))
                    OutlinedTextField(value = shots,
                        onValueChange = { shots = it.filter { c -> c.isDigit() || c == '-' } },
                        label = { Text("Shots") },
                        placeholder = { Text("-1=inf") },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Number),
                        modifier = Modifier.weight(1f))
                }
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp)
                        .clickable { gag = !gag }
                ) {
                    Switch(checked = gag, onCheckedChange = { gag = it })
                    Spacer(Modifier.width(8.dp))
                    Text("Gag (suppress line)", style = MaterialTheme.typography.bodyLarge)
                }
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp)
                        .clickable { hilite = !hilite }
                ) {
                    Switch(checked = hilite, onCheckedChange = { hilite = it })
                    Spacer(Modifier.width(8.dp))
                    Text("Highlight match", style = MaterialTheme.typography.bodyLarge)
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                if (name.isNotBlank() && pattern.isNotBlank() && patternError == null) {
                    onSave(Trigger(
                        name = name.trim(),
                        pattern = pattern.trim(),
                        body = body.trim(),
                        priority = priority.trim().toIntOrNull() ?: 0,
                        shots = shots.trim().toIntOrNull() ?: -1,
                        gag = gag,
                        hilite = hilite,
                    ))
                }
            }) { Text("Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

@Composable
fun SettingsDialog(
    settings: AppSettings,
    onDismiss: () -> Unit,
) {
    var fontSize by remember { mutableStateOf(settings.fontSize.toString()) }
    var fontSizeLand by remember { mutableStateOf(settings.fontSizeLandscape.toString()) }
    var scrollback by remember { mutableStateOf(settings.scrollbackLines.toString()) }
    var defaultPort by remember { mutableStateOf(settings.defaultPort.toString()) }
    var defaultSsl by remember { mutableStateOf(settings.defaultSsl) }
    var keepScreenOn by remember { mutableStateOf(settings.keepScreenOn) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Settings") },
        text = {
            Column(
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.verticalScroll(rememberScrollState())
            ) {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    OutlinedTextField(value = fontSize,
                        onValueChange = { fontSize = it.filter { c -> c.isDigit() } },
                        label = { Text("Font size") },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Number),
                        modifier = Modifier.weight(1f))
                    OutlinedTextField(value = fontSizeLand,
                        onValueChange = { fontSizeLand = it.filter { c -> c.isDigit() } },
                        label = { Text("Font (land)") },
                        singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Number),
                        modifier = Modifier.weight(1f))
                }
                OutlinedTextField(value = scrollback,
                    onValueChange = { scrollback = it.filter { c -> c.isDigit() } },
                    label = { Text("Scrollback lines") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(value = defaultPort,
                    onValueChange = { defaultPort = it.filter { c -> c.isDigit() } },
                    label = { Text("Default port") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = androidx.compose.ui.text.input.KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth())
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp)
                        .clickable { defaultSsl = !defaultSsl }
                ) {
                    Switch(checked = defaultSsl, onCheckedChange = { defaultSsl = it })
                    Spacer(Modifier.width(8.dp))
                    Text("Default SSL/TLS", style = MaterialTheme.typography.bodyLarge)
                }
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp)
                        .clickable { keepScreenOn = !keepScreenOn }
                ) {
                    Switch(checked = keepScreenOn, onCheckedChange = { keepScreenOn = it })
                    Spacer(Modifier.width(8.dp))
                    Text("Keep screen on", style = MaterialTheme.typography.bodyLarge)
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                settings.fontSize = fontSize.toIntOrNull()?.coerceIn(8, 32) ?: 14
                settings.fontSizeLandscape = fontSizeLand.toIntOrNull()?.coerceIn(8, 32) ?: 12
                settings.scrollbackLines = scrollback.toIntOrNull()?.coerceIn(1000, 100000) ?: 20000
                settings.defaultPort = defaultPort.toIntOrNull()?.coerceIn(1, 65535) ?: 4201
                settings.defaultSsl = defaultSsl
                settings.keepScreenOn = keepScreenOn
                onDismiss()
            }) { Text("Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}
