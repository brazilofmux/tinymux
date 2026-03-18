package org.tinymux.titan.ui

import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.launch
import org.tinymux.titan.net.AnsiParser
import org.tinymux.titan.net.MudConnection

import androidx.compose.runtime.snapshots.SnapshotStateList

class WorldTab(
    val name: String,
    var connection: MudConnection? = null,
    val lines: SnapshotStateList<AnnotatedString> = mutableStateListOf(),
    val history: MutableList<String> = mutableListOf(),
    var hasActivity: Boolean = false,
    var disconnected: Boolean = false,
)

@Composable
fun TitanApp() {
    val tabs = remember { mutableStateListOf(WorldTab("(System)")) }
    var activeTab by remember { mutableIntStateOf(0) }
    var inputText by remember { mutableStateOf("") }
    var historyPos by remember { mutableIntStateOf(-1) }
    var savedInput by remember { mutableStateOf("") }
    val scope = rememberCoroutineScope()
    val listState = rememberLazyListState()
    val focusRequester = remember { FocusRequester() }
    var showConnectDialog by remember { mutableStateOf(false) }

    val config = LocalConfiguration.current
    val isLandscape = config.screenWidthDp > config.screenHeightDp
    val fontSize = if (isLandscape) 12.sp else 14.sp
    val monoStyle = TextStyle(fontFamily = FontFamily.Monospace, fontSize = fontSize, color = Color(0xFFC0C0C0))

    fun currentTab() = tabs.getOrNull(activeTab)

    fun appendLine(tabIndex: Int, line: String) {
        val parsed = AnsiParser.parse(line)
        tabs.getOrNull(tabIndex)?.let { tab ->
            tab.lines.add(parsed)
            while (tab.lines.size > 20000) tab.lines.removeAt(0)
            if (tabIndex != activeTab) tab.hasActivity = true
        }
    }

    fun connectWorld(name: String, host: String, port: Int, ssl: Boolean) {
        val tab = WorldTab(name)
        tabs.add(tab)
        val tabIndex = tabs.size - 1
        activeTab = tabIndex

        val conn = MudConnection(name, host, port, ssl)
        tab.connection = conn
        conn.onLine = { line -> scope.launch { appendLine(tabIndex, line) } }
        conn.onConnect = { scope.launch {
            appendLine(tabIndex, "% Connected to $host:$port")
            tab.disconnected = false
        }}
        conn.onDisconnect = { scope.launch {
            appendLine(tabIndex, "% Connection lost.")
            tab.disconnected = true
        }}
        appendLine(tabIndex, "% Connecting to $host:$port${if (ssl) " (ssl)" else ""}...")
        conn.connect(scope)
    }

    fun handleInput(text: String) {
        if (text.isBlank()) return
        val tab = currentTab() ?: return

        // Save to history
        tab.history.add(0, text)
        if (tab.history.size > 500) tab.history.removeLast()
        historyPos = -1

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
        appendLine(0, "Tap Connect to connect to a world.")
        focusRequester.requestFocus()
    }

    // Auto-scroll when new lines arrive
    val lineCount = currentTab()?.lines?.size ?: 0
    LaunchedEffect(lineCount) {
        if (lineCount > 0) listState.animateScrollToItem(lineCount - 1)
    }

    Column(modifier = Modifier
        .fillMaxSize()
        .background(Color.Black)
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
            ToolbarButton("DC") {
                if (activeTab > 0) {
                    tabs.getOrNull(activeTab)?.connection?.disconnect()
                    tabs.removeAt(activeTab)
                    activeTab = (activeTab - 1).coerceAtLeast(0)
                }
            }
            if (!isLandscape) {
                Spacer(Modifier.weight(1f))
                ToolbarButton("Clear") { currentTab()?.lines?.clear() }
            }
        }

        // Tab bar
        ScrollableTabRow(
            selectedTabIndex = activeTab,
            modifier = Modifier.fillMaxWidth(),
            containerColor = Color(0xFF303030),
            contentColor = Color.White,
            edgePadding = 0.dp,
        ) {
            tabs.forEachIndexed { index, tab ->
                Tab(
                    selected = index == activeTab,
                    onClick = {
                        activeTab = index
                        tab.hasActivity = false
                    },
                    text = {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            if (tab.hasActivity && index != activeTab) {
                                Box(Modifier
                                    .size(6.dp)
                                    .padding(end = 4.dp)
                                    .background(Color(0xFFFFC800), shape = androidx.compose.foundation.shape.CircleShape))
                                Spacer(Modifier.width(4.dp))
                            }
                            Text(
                                tab.name,
                                fontSize = 12.sp,
                                color = if (tab.disconnected) Color(0xFF806060)
                                        else if (index == activeTab) Color.White
                                        else Color(0xFFA0A0A0)
                            )
                        }
                    }
                )
            }
        }

        // Output pane
        LazyColumn(
            state = listState,
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(horizontal = 4.dp),
        ) {
            val lines = currentTab()?.lines ?: emptyList()
            items(lines) { line ->
                Text(
                    text = line,
                    style = monoStyle,
                    modifier = Modifier.fillMaxWidth()
                )
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
                    .focusRequester(focusRequester),
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
            onConnect = { host, port, ssl ->
                showConnectDialog = false
                connectWorld("$host:$port", host, port, ssl)
            },
            onDismiss = { showConnectDialog = false }
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
fun ConnectDialog(onConnect: (String, Int, Boolean) -> Unit, onDismiss: () -> Unit) {
    var host by remember { mutableStateOf("") }
    var port by remember { mutableStateOf("4201") }
    var ssl by remember { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Connect") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(value = host, onValueChange = { host = it },
                    label = { Text("Host") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                OutlinedTextField(value = port, onValueChange = { port = it },
                    label = { Text("Port") }, singleLine = true,
                    modifier = Modifier.fillMaxWidth())
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(checked = ssl, onCheckedChange = { ssl = it })
                    Text("SSL/TLS")
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                if (host.isNotBlank()) onConnect(host.trim(), port.trim().toIntOrNull() ?: 4201, ssl)
            }) { Text("Connect") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}
