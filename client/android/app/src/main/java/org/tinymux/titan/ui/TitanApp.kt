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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.launch
import org.tinymux.titan.data.World
import org.tinymux.titan.data.WorldRepository
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
    val context = LocalContext.current
    val worldRepo = remember { WorldRepository(context) }

    val tabs = remember { mutableStateListOf(WorldTab("(System)")) }
    var activeTab by remember { mutableIntStateOf(0) }
    var inputText by remember { mutableStateOf("") }
    var historyPos by remember { mutableIntStateOf(-1) }
    var savedInput by remember { mutableStateOf("") }
    val scope = rememberCoroutineScope()
    val listState = rememberLazyListState()
    val focusRequester = remember { FocusRequester() }
    var showConnectDialog by remember { mutableStateOf(false) }
    var showWorldManager by remember { mutableStateOf(false) }

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
        conn.onLine = { line -> appendLine(tabIndex, line) }
        conn.onConnect = {
            appendLine(tabIndex, "% Connected to $host:$port")
            tab.disconnected = false
        }
        conn.onDisconnect = {
            appendLine(tabIndex, "% Connection lost.")
            tab.disconnected = true
        }
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
        appendLine(0, "Tap Connect or Worlds to get started.")
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
                        .padding(horizontal = 12.dp, vertical = 6.dp),
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
                }
                // Separator
                Box(Modifier.width(1.dp).height(20.dp).background(Color(0xFF505050)))
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
            worldRepo = worldRepo,
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
                connectWorld(world.name, world.host, world.port, world.ssl)
            },
            onDismiss = { showWorldManager = false }
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
fun ConnectDialog(
    worldRepo: WorldRepository,
    onConnect: (host: String, port: Int, ssl: Boolean, saveName: String) -> Unit,
    onDismiss: () -> Unit,
) {
    var host by remember { mutableStateOf("") }
    var port by remember { mutableStateOf("") }
    var ssl by remember { mutableStateOf(false) }
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
                if (host.isNotBlank()) onConnect(host.trim(), port.trim().toIntOrNull() ?: 4201, ssl, saveName.trim())
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
                    ))
                }
            }) { Text("Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}
