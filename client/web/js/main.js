// main.js -- Titan web client entry point.
'use strict';

const app = {
    connections: {},    // name -> Connection
    tabs: [],           // [{name, conn, active, disconnected}]
    activeTab: null,    // name
    history: {},        // name -> [lines]
    historyPos: {},     // name -> int
    savedInput: {},     // name -> string
    triggerDB: new TriggerDB(),
    timerDB: new TimerDB(),
    hookDB: new HookDB(),
    spawnDB: new SpawnDB(),
    variables: new VariableStore(),
    mcpParsers: {},     // name -> McpParser
    spawnLines: {},     // tabName -> {spawnPath -> [lines]}
    activeSpawn: {},    // tabName -> spawnPath ('' = main)
    logActive: false,
    logLines: [],
};

// Built-in commands for auto-complete
const COMMANDS = [
    '/connect', '/disconnect', '/worlds', '/find', '/clear',
    '/triggers', '/def', '/undef', '/list', '/help',
    '/repeat', '/killtimer', '/timers',
    '/hook', '/unhook', '/hooks',
    '/spawn', '/log', '/speak',
    '/set', '/unset', '/vars',
];

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

// -- Tab Management --

function addTab(name) {
    if (app.tabs.find(t => t.name === name)) return;
    app.tabs.push({ name, active: false });
    app.history[name] = [];
    app.historyPos[name] = -1;
    renderTabs();
    switchTab(name);
}

function removeTab(name) {
    if (name === '(System)') return;
    const conn = app.connections[name];
    if (conn) { conn.disconnect(); delete app.connections[name]; }
    app.tabs = app.tabs.filter(t => t.name !== name);
    if (app.activeTab === name) {
        switchTab(app.tabs.length > 0 ? app.tabs[0].name : null);
    }
    renderTabs();
}

function switchTab(name) {
    app.activeTab = name;
    // Clear activity
    const tab = app.tabs.find(t => t.name === name);
    if (tab) tab.active = false;
    renderTabs();
    renderOutput();
    updateStatus();
    $('#inputline').focus();
}

function renderTabs() {
    const bar = $('#tabbar');
    bar.innerHTML = '';
    for (const tab of app.tabs) {
        const el = document.createElement('div');
        el.className = 'tab' + (tab.name === app.activeTab ? ' active' : '') +
                       (tab.active ? ' has-activity' : '') +
                       (tab.disconnected ? ' disconnected' : '');

        let html = `<span class="activity-dot"></span>${escapeHtml(tab.name)}`;
        if (tab.disconnected) {
            html += `<span class="reconnect-btn" title="Reconnect">\u21bb</span>`;
        }
        if (tab.name !== '(System)') {
            html += `<span class="close-btn">\u00d7</span>`;
        }
        el.innerHTML = html;

        el.addEventListener('click', (e) => {
            if (e.target.classList.contains('close-btn')) {
                removeTab(tab.name);
            } else if (e.target.classList.contains('reconnect-btn')) {
                reconnectTab(tab.name);
            } else {
                switchTab(tab.name);
            }
        });
        el.addEventListener('auxclick', (e) => {
            if (e.button === 1) removeTab(tab.name);
        });
        bar.appendChild(el);
    }
}

function reconnectTab(name) {
    const conn = app.connections[name];
    if (!conn) return;
    // Recreate the connection with the same parameters
    const newConn = new Connection(name, conn.host, conn.port, conn.ssl);
    app.connections[name] = newConn;
    const tab = app.tabs.find(t => t.name === name);

    // Re-wire MCP
    const mcp = new McpParser();
    app.mcpParsers[name] = mcp;
    mcp.sendRaw = (raw) => newConn.sendLine(raw);
    mcp.onEditRequest = (ref, editName, type, content) => showMcpEditor(name, ref, editName, type, content);

    newConn.onLine = (line) => {
        if (mcp.processLine(line)) return;
        const tr = app.triggerDB.check(line);
        for (const cmd of tr.commands) handleInput(cmd);
        if (tr.speakText && window.speechSynthesis) {
            speechSynthesis.speak(new SpeechSynthesisUtterance(tr.speakText));
        }
        if (!tr.gagged) {
            const display = tr.displayLine || line;
            appendLine(name, display);
            const plain = stripAnsi(display);
            for (const path of app.spawnDB.match(plain)) {
                if (!app.spawnLines[name][path]) app.spawnLines[name][path] = [];
                app.spawnLines[name][path].push(display);
            }
        }
        if (app.logActive) app.logLines.push(stripAnsi(line));
    };
    newConn.onPrompt = (prompt) => appendLine(name, prompt);
    newConn.onConnect = () => {
        if (tab) tab.disconnected = false;
        appendLine(name, '% Reconnected.');
        updateStatus();
        renderTabs();
        for (const cmd of app.hookDB.fireEvent('CONNECT')) newConn.sendLine(cmd);
    };
    newConn.onDisconnect = () => {
        if (tab) tab.disconnected = true;
        appendLine(name, '% Connection lost.');
        app.timerDB.cancelAll();
        for (const cmd of app.hookDB.fireEvent('DISCONNECT')) appendLine(name, '% [hook] ' + cmd);
        updateStatus();
        renderTabs();
    };

    appendLine(name, '% Reconnecting...');
    newConn.connect();
}

// -- Output Rendering --

function appendLine(tabName, line) {
    const conn = app.connections[tabName];
    if (!conn) {
        // System tab — store in a virtual scrollback
        if (!app._systemLines) app._systemLines = [];
        app._systemLines.push(line);
        if (app._systemLines.length > 20000) app._systemLines.shift();
    }
    if (tabName === app.activeTab) {
        const output = $('#output');
        const div = document.createElement('div');
        div.className = 'line';
        div.innerHTML = renderAnsiLine(line);
        output.appendChild(div);
        // Auto-scroll if near bottom
        if (output.scrollTop + output.clientHeight >= output.scrollHeight - 50) {
            output.scrollTop = output.scrollHeight;
        }
    } else {
        // Background activity
        const tab = app.tabs.find(t => t.name === tabName);
        if (tab) { tab.active = true; renderTabs(); }
    }
}

function renderOutput() {
    const output = $('#output');
    output.innerHTML = '';
    let lines;
    if (app.activeTab === '(System)') {
        lines = app._systemLines || [];
    } else {
        const conn = app.connections[app.activeTab];
        lines = conn ? conn.scrollback : [];
    }
    for (const line of lines) {
        const div = document.createElement('div');
        div.className = 'line';
        div.innerHTML = renderAnsiLine(line);
        output.appendChild(div);
    }
    output.scrollTop = output.scrollHeight;
}

function updateStatus() {
    const bar = $('#statusbar');
    if (!app.activeTab) { bar.textContent = '(no connection)'; return; }
    let s = app.activeTab;
    const conn = app.connections[app.activeTab];
    if (conn) {
        if (conn.ssl) s += ' [ssl]';
        if (!conn.connected) s += ' (disconnected)';
    }
    const nconn = Object.values(app.connections).filter(c => c.connected).length;
    if (nconn > 1) s += `  [${nconn} conn]`;
    if (app.logActive) s += ' [log]';
    bar.textContent = s;
}

// -- Connection --

function connectWorld(name, host, port, ssl, loginCommands = []) {
    addTab(name);
    const conn = new Connection(name, host, port, ssl);
    app.connections[name] = conn;
    app.spawnLines[name] = {};
    app.activeSpawn[name] = '';

    // MCP parser per connection
    const mcp = new McpParser();
    app.mcpParsers[name] = mcp;
    mcp.sendRaw = (raw) => conn.sendLine(raw);
    mcp.onEditRequest = (ref, editName, type, content) => {
        showMcpEditor(name, ref, editName, type, content);
    };

    const tab = app.tabs.find(t => t.name === name);
    conn.onLine = (line) => {
        // MCP intercept
        if (mcp.processLine(line)) return;

        const tr = app.triggerDB.check(line);
        for (const cmd of tr.commands) handleInput(cmd);
        if (tr.speakText && window.speechSynthesis) {
            const utter = new SpeechSynthesisUtterance(tr.speakText);
            speechSynthesis.speak(utter);
        }
        if (!tr.gagged) {
            const display = tr.displayLine || line;
            appendLine(name, display);
            // Route to spawns
            const plain = stripAnsi(display);
            const matched = app.spawnDB.match(plain);
            for (const path of matched) {
                if (!app.spawnLines[name][path]) app.spawnLines[name][path] = [];
                app.spawnLines[name][path].push(display);
                while (app.spawnLines[name][path].length > 20000) app.spawnLines[name][path].shift();
            }
        }
        if (app.logActive) {
            app.logLines.push(stripAnsi(line));
        }
    };
    conn.onPrompt = (prompt) => appendLine(name, prompt);
    conn.onConnect = () => {
        if (tab) tab.disconnected = false;
        appendLine(name, '% Connected to ' + host + ':' + port);
        updateStatus();
        renderTabs();
        for (const cmd of app.hookDB.fireEvent('CONNECT')) conn.sendLine(cmd);
        for (const cmd of loginCommands) conn.sendLine(cmd);
    };
    conn.onDisconnect = () => {
        if (tab) tab.disconnected = true;
        appendLine(name, '% Connection lost.');
        app.timerDB.cancelAll();
        for (const cmd of app.hookDB.fireEvent('DISCONNECT')) {
            appendLine(name, '% [hook] ' + cmd);
        }
        updateStatus();
        renderTabs();
    };

    if (!conn.connect()) {
        appendLine(name, '% Failed to connect to ' + host + ':' + port);
    } else {
        appendLine(name, '% Connecting to ' + host + ':' + port + (ssl ? ' (ssl)' : '') + '...');
    }

    // Auto-save to worlds
    Settings.addWorld({ name, host, port, ssl });
}

// -- Input --

function handleInput(text) {
    if (!text) return;

    // Save to history
    const hist = app.history[app.activeTab] || [];
    hist.unshift(text);
    if (hist.length > 500) hist.pop();
    app.history[app.activeTab] = hist;
    app.historyPos[app.activeTab] = -1;

    // Handle local /commands
    if (text.startsWith('/')) {
        const parts = text.split(/\s+/);
        const cmd = parts[0].toLowerCase();
        switch (cmd) {
        case '/connect':
            showConnectDialog();
            return;
        case '/worlds':
            showWorldsDialog();
            return;
        case '/disconnect':
            if (app.activeTab && app.activeTab !== '(System)') removeTab(app.activeTab);
            return;
        case '/triggers':
            showTriggersDialog();
            return;
        case '/find':
            showFindDialog();
            return;
        case '/clear':
            $('#btn-clear').click();
            return;
        case '/def': {
            // /def name -t'pattern' body
            // Quick parse: /def name pattern body
            if (parts.length >= 4) {
                app.triggerDB.add({ name: parts[1], pattern: parts[2], body: parts.slice(3).join(' ') });
                Settings.set('triggers', app.triggerDB.toJSON());
                appendLine(app.activeTab, '% Trigger defined: ' + parts[1]);
            } else {
                appendLine(app.activeTab, '% Usage: /def <name> <pattern> <action>');
            }
            return;
        }
        case '/undef':
            if (parts[1]) {
                app.triggerDB.remove(parts[1]);
                Settings.set('triggers', app.triggerDB.toJSON());
                appendLine(app.activeTab, '% Removed: ' + parts[1]);
            }
            return;
        case '/list':
            for (const t of app.triggerDB.list()) {
                appendLine(app.activeTab, `%   ${t.name}  /${t.pattern}/ → ${t.body}${t.gag ? ' [gag]' : ''}`);
            }
            if (app.triggerDB.list().length === 0) appendLine(app.activeTab, '% No triggers defined.');
            return;
        case '/repeat': {
            // /repeat name seconds command
            if (parts.length >= 4) {
                const sec = parseFloat(parts[2]);
                if (sec > 0) {
                    app.timerDB.add(parts[1], parts.slice(3).join(' '), sec * 1000);
                    appendLine(app.activeTab, `% Timer '${parts[1]}' set: every ${sec}s`);
                } else {
                    appendLine(app.activeTab, '% Invalid interval.');
                }
            } else {
                appendLine(app.activeTab, '% Usage: /repeat <name> <seconds> <command>');
            }
            return;
        }
        case '/killtimer':
        case '/cancel':
            if (parts[1]) {
                if (app.timerDB.remove(parts[1])) appendLine(app.activeTab, `% Timer '${parts[1]}' cancelled.`);
                else appendLine(app.activeTab, `% No timer named '${parts[1]}'.`);
            } else {
                appendLine(app.activeTab, '% Usage: /killtimer <name>');
            }
            return;
        case '/timers': {
            const tl = app.timerDB.list();
            if (!tl.length) { appendLine(app.activeTab, '% No active timers.'); }
            else {
                appendLine(app.activeTab, '% Active timers:');
                for (const t of tl) {
                    const shots = t.shotsRemaining < 0 ? 'inf' : t.shotsRemaining;
                    appendLine(app.activeTab, `%   ${t.name}: every ${t.intervalMs/1000}s, shots=${shots} -> ${t.command}`);
                }
            }
            return;
        }
        case '/hook': {
            // /hook name event = command
            const hArgs = text.slice(6);
            const eq = hArgs.indexOf('=');
            if (eq < 0) {
                appendLine(app.activeTab, '% Usage: /hook <name> <event> = <command>');
                appendLine(app.activeTab, '% Events: ' + HookDB.EVENTS.join(', '));
            } else {
                const before = hArgs.slice(0, eq).trim().split(/\s+/);
                const body = hArgs.slice(eq + 1).trim();
                if (before.length >= 2) {
                    const event = before[1].toUpperCase();
                    if (HookDB.EVENTS.includes(event)) {
                        app.hookDB.add({ name: before[0], event, body });
                        Settings.set('hooks', app.hookDB.toJSON());
                        appendLine(app.activeTab, `% Hook '${before[0]}' on ${event} -> ${body}`);
                    } else {
                        appendLine(app.activeTab, `% Unknown event '${event}'.`);
                    }
                } else {
                    appendLine(app.activeTab, '% Usage: /hook <name> <event> = <command>');
                }
            }
            return;
        }
        case '/unhook':
            if (parts[1]) {
                app.hookDB.remove(parts[1]);
                Settings.set('hooks', app.hookDB.toJSON());
                appendLine(app.activeTab, `% Hook '${parts[1]}' removed.`);
            } else {
                appendLine(app.activeTab, '% Usage: /unhook <name>');
            }
            return;
        case '/hooks': {
            const hl = app.hookDB.list();
            if (!hl.length) appendLine(app.activeTab, '% No hooks defined.');
            else {
                appendLine(app.activeTab, '% Hooks:');
                for (const h of hl) appendLine(app.activeTab, `%   ${h.name}: ${h.event} -> ${h.body}`);
            }
            return;
        }
        case '/spawn': {
            const sub = (parts[1] || '').toLowerCase();
            if (sub === 'add' && parts.length >= 4) {
                try {
                    new RegExp(parts[3]);
                    app.spawnDB.add({ name: parts[2], patterns: [parts[3]] });
                    Settings.set('spawns', app.spawnDB.toJSON());
                    appendLine(app.activeTab, `% Spawn '${parts[2]}' added: /${parts[3]}/`);
                } catch (e) {
                    appendLine(app.activeTab, '% Bad pattern: ' + e.message);
                }
            } else if ((sub === 'remove' || sub === 'del') && parts[2]) {
                app.spawnDB.remove(parts[2].toLowerCase());
                Settings.set('spawns', app.spawnDB.toJSON());
                appendLine(app.activeTab, `% Spawn '${parts[2]}' removed.`);
            } else if (sub === 'list' || !sub) {
                const sl = app.spawnDB.list();
                if (!sl.length) appendLine(app.activeTab, '% No spawns defined.');
                else {
                    appendLine(app.activeTab, '% Spawns:');
                    for (const s of sl) appendLine(app.activeTab, `%   ${s.name} (${s.path}): ${s.patterns.map(p => '/' + p + '/').join(', ')}`);
                }
            } else if ((sub === 'focus' || sub === 'fg') && parts[2]) {
                const sp = parts[2].toLowerCase() === 'main' ? '' : parts[2].toLowerCase();
                app.activeSpawn[app.activeTab] = sp;
                renderOutput();
            } else {
                appendLine(app.activeTab, '% Usage: /spawn [add|remove|list|focus] ...');
            }
            return;
        }
        case '/log':
            if (app.logActive) {
                app.logActive = false;
                // Download log
                const blob = new Blob([app.logLines.join('\n')], { type: 'text/plain' });
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = `titan_${new Date().toISOString().replace(/[:.]/g, '-')}.log`;
                a.click();
                URL.revokeObjectURL(url);
                appendLine(app.activeTab, '% Logging stopped. File downloaded.');
                app.logLines = [];
            } else {
                app.logActive = true;
                app.logLines = [];
                appendLine(app.activeTab, '% Logging started. Use /log again to stop and download.');
            }
            updateStatus();
            return;
        case '/speak':
            if (parts.length > 1 && window.speechSynthesis) {
                const utter = new SpeechSynthesisUtterance(parts.slice(1).join(' '));
                speechSynthesis.speak(utter);
            } else {
                appendLine(app.activeTab, '% Usage: /speak <text>');
            }
            return;
        case '/set': {
            if (parts.length >= 3) {
                const k = parts[1], v = parts.slice(2).join(' ');
                if (k.startsWith('temp.')) app.variables.temp[k.slice(5)] = v;
                else if (k.startsWith('worldtemp.')) app.variables.worldTemp[k.slice(10)] = v;
                else app.variables.temp[k] = v;
                appendLine(app.activeTab, `% Set ${k} = ${v}`);
            } else {
                appendLine(app.activeTab, '% Usage: /set <var> <value>');
            }
            return;
        }
        case '/unset':
            if (parts[1]) {
                const k = parts[1];
                delete app.variables.temp[k.replace(/^temp\./, '')];
                delete app.variables.worldTemp[k.replace(/^worldtemp\./, '')];
                appendLine(app.activeTab, `% Unset ${k}`);
            } else {
                appendLine(app.activeTab, '% Usage: /unset <var>');
            }
            return;
        case '/vars': {
            const t = Object.entries(app.variables.temp);
            const wt = Object.entries(app.variables.worldTemp);
            if (!t.length && !wt.length) appendLine(app.activeTab, '% No user variables set.');
            else {
                appendLine(app.activeTab, '% Variables:');
                for (const [k,v] of t) appendLine(app.activeTab, `%   temp.${k} = ${v}`);
                for (const [k,v] of wt) appendLine(app.activeTab, `%   worldtemp.${k} = ${v}`);
            }
            return;
        }
        case '/help':
            appendLine(app.activeTab, '% Commands:');
            appendLine(app.activeTab, '%   /connect                       - Connect dialog');
            appendLine(app.activeTab, '%   /disconnect                    - Close connection');
            appendLine(app.activeTab, '%   /worlds                        - World Manager');
            appendLine(app.activeTab, '%   /triggers                      - Trigger Manager');
            appendLine(app.activeTab, '%   /def <name> <pattern> <action> - Define trigger');
            appendLine(app.activeTab, '%   /undef <name>                  - Remove trigger');
            appendLine(app.activeTab, '%   /list                          - List triggers');
            appendLine(app.activeTab, '%   /find                          - Find in scrollback');
            appendLine(app.activeTab, '%   /log                           - Toggle logging');
            appendLine(app.activeTab, '%   /repeat <name> <sec> <cmd>     - Create timer');
            appendLine(app.activeTab, '%   /killtimer <name>              - Cancel timer');
            appendLine(app.activeTab, '%   /timers                        - List timers');
            appendLine(app.activeTab, '%   /hook <name> <event> = <cmd>   - Define hook');
            appendLine(app.activeTab, '%   /unhook <name>                 - Remove hook');
            appendLine(app.activeTab, '%   /hooks                         - List hooks');
            appendLine(app.activeTab, '%   /spawn add <name> <pattern>    - Add spawn');
            appendLine(app.activeTab, '%   /spawn remove <name>           - Remove spawn');
            appendLine(app.activeTab, '%   /spawn list                    - List spawns');
            appendLine(app.activeTab, '%   /spawn focus <name|main>       - Switch view');
            appendLine(app.activeTab, '%   /set <var> <value>             - Set variable');
            appendLine(app.activeTab, '%   /unset <var>                   - Remove variable');
            appendLine(app.activeTab, '%   /vars                          - List variables');
            appendLine(app.activeTab, '%   /speak <text>                  - Text-to-speech');
            appendLine(app.activeTab, '%   /clear                         - Clear output');
            appendLine(app.activeTab, '%   /help                          - This help');
            return;
        }
        // Unknown /command — don't send to server, show error
        // Actually, some MUDs use / commands, so send it through
    }

    const conn = app.connections[app.activeTab];
    if (conn && conn.connected) {
        conn.sendLine(text);
        if (!conn.telnet.remoteEcho) {
            appendLine(app.activeTab, '> ' + text);
        }
    } else {
        appendLine(app.activeTab, '> ' + text);
    }
}

// -- Dialogs --

function showConnectDialog() {
    const dlg = $('#connect-dialog');
    $('#conn-host').value = '';
    $('#conn-port').value = '4201';
    $('#conn-ssl').checked = false;
    dlg.showModal();
}

function showWorldsDialog() {
    const dlg = $('#worlds-dialog');
    refreshWorldsList();
    dlg.showModal();
}

function refreshWorldsList() {
    const sel = $('#worlds-list');
    sel.innerHTML = '';
    for (const w of Settings.getWorlds()) {
        const opt = document.createElement('option');
        opt.value = w.name;
        opt.textContent = `${w.name}  ${w.host}:${w.port}${w.ssl ? ' ssl' : ''}`;
        sel.appendChild(opt);
    }
}

function showWorldEditDialog(world) {
    const dlg = $('#world-edit-dialog');
    $('#world-edit-title').textContent = world ? 'Edit World' : 'Add World';
    $('#we-name').value = world ? world.name : '';
    $('#we-host').value = world ? world.host : '';
    $('#we-port').value = world ? world.port : '4201';
    $('#we-char').value = world ? (world.character || '') : '';
    $('#we-ssl').checked = world ? world.ssl : false;
    dlg.showModal();
    return new Promise((resolve) => {
        dlg._resolve = resolve;
    });
}

function showTriggersDialog() {
    refreshTriggersList();
    $('#triggers-dialog').showModal();
}

function refreshTriggersList() {
    const sel = $('#triggers-list');
    sel.innerHTML = '';
    for (const t of app.triggerDB.list()) {
        const opt = document.createElement('option');
        opt.value = t.name;
        let desc = t.name + '  /' + t.pattern + '/';
        if (t.gag) desc += ' [gag]';
        if (t.body) desc += ' → ' + t.body;
        opt.textContent = desc;
        sel.appendChild(opt);
    }
}

function showTrigEditDialog(trig) {
    $('#trig-edit-title').textContent = trig ? 'Edit Trigger' : 'Add Trigger';
    $('#te-name').value = trig ? trig.name : '';
    $('#te-pattern').value = trig ? trig.pattern : '';
    $('#te-body').value = trig ? trig.body : '';
    $('#te-priority').value = trig ? trig.priority : 0;
    $('#te-shots').value = trig ? trig.shots : -1;
    $('#te-gag').checked = trig ? trig.gag : false;
    $('#trig-edit-dialog').showModal();
}

function showFindDialog() {
    const dlg = $('#find-dialog');
    $('#find-text').value = '';
    dlg.showModal();
    $('#find-text').focus();
}

function findInScrollback(pattern, searchUp) {
    if (!pattern) return;
    const output = $('#output');
    const lines = output.querySelectorAll('.line');
    // Remove existing highlights
    lines.forEach(l => l.classList.remove('find-highlight'));

    const search = pattern.toLowerCase();
    if (searchUp) {
        for (let i = lines.length - 1; i >= 0; i--) {
            if (lines[i].textContent.toLowerCase().includes(search)) {
                lines[i].classList.add('find-highlight');
                lines[i].scrollIntoView({ block: 'center' });
                return;
            }
        }
    } else {
        for (let i = 0; i < lines.length; i++) {
            if (lines[i].textContent.toLowerCase().includes(search)) {
                lines[i].classList.add('find-highlight');
                lines[i].scrollIntoView({ block: 'center' });
                return;
            }
        }
    }
}

// -- Keyboard --

function handleKeyDown(e) {
    const input = $('#inputline');
    // Avoid Ctrl+N (new window), Ctrl+F (browser find) — don't override.
    // Use Ctrl+Shift+N for connect, Ctrl+Shift+F for find instead.
    if (e.ctrlKey && e.shiftKey && e.key === 'N') { e.preventDefault(); showConnectDialog(); }
    else if (e.ctrlKey && e.shiftKey && e.key === 'F') { e.preventDefault(); showFindDialog(); }
    else if (e.ctrlKey && e.key === 'Tab') {
        e.preventDefault();
        const idx = app.tabs.findIndex(t => t.name === app.activeTab);
        if (e.shiftKey) {
            switchTab(app.tabs[(idx - 1 + app.tabs.length) % app.tabs.length].name);
        } else {
            switchTab(app.tabs[(idx + 1) % app.tabs.length].name);
        }
    }

    // History in input
    if (document.activeElement === input) {
        const tabName = app.activeTab;
        const hist = app.history[tabName] || [];
        if (e.key === 'ArrowUp' && hist.length > 0) {
            e.preventDefault();
            let pos = app.historyPos[tabName];
            if (pos < 0) { app.savedInput[tabName] = input.value; pos = 0; }
            else if (pos < hist.length - 1) pos++;
            else return;
            app.historyPos[tabName] = pos;
            input.value = hist[pos];
        } else if (e.key === 'ArrowDown') {
            e.preventDefault();
            let pos = app.historyPos[tabName];
            if (pos < 0) return;
            pos--;
            app.historyPos[tabName] = pos;
            input.value = pos < 0 ? (app.savedInput[tabName] || '') : hist[pos];
        } else if (e.key === 'PageUp') {
            e.preventDefault();
            $('#output').scrollBy(0, -$('#output').clientHeight + 20);
        } else if (e.key === 'PageDown') {
            e.preventDefault();
            $('#output').scrollBy(0, $('#output').clientHeight - 20);
        }
    }
}

// -- Helpers --

function stripAnsi(text) {
    return text.replace(/\x1b\[[0-9;]*[A-Za-z]/g, '');
}

function showMcpEditor(tabName, reference, name, type, content) {
    const dlg = $('#mcp-editor-dialog');
    if (!dlg) return; // dialog not in HTML yet
    $('#mcp-edit-title').textContent = 'Edit: ' + name;
    $('#mcp-edit-text').value = content;
    dlg._tabName = tabName;
    dlg._reference = reference;
    dlg._type = type;
    dlg.showModal();
}

// -- Init --

function init() {
    Settings.load();
    app._systemLines = [];

    // System tab
    addTab('(System)');
    appendLine('(System)', 'Titan for Web');
    appendLine('(System)', 'Click Connect to connect to a world, or Worlds to manage saved worlds.');

    // Input handler
    const input = $('#inputline');
    input.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            e.preventDefault();
            handleInput(input.value);
            input.value = '';
        }
    });

    // Global keyboard shortcuts
    document.addEventListener('keydown', handleKeyDown);

    // Connect dialog
    $('#conn-ok').addEventListener('click', () => {
        const host = $('#conn-host').value.trim();
        const port = $('#conn-port').value.trim() || '4201';
        const ssl = $('#conn-ssl').checked;
        if (host) {
            connectWorld(host + ':' + port, host, port, ssl);
            $('#connect-dialog').close();
        }
    });
    $('#conn-cancel').addEventListener('click', () => $('#connect-dialog').close());

    // Worlds dialog
    $('#worlds-add').addEventListener('click', async () => {
        showWorldEditDialog(null);
    });
    $('#worlds-edit').addEventListener('click', () => {
        const sel = $('#worlds-list');
        if (sel.selectedIndex >= 0) {
            const worlds = Settings.getWorlds();
            showWorldEditDialog(worlds[sel.selectedIndex]);
        }
    });
    $('#worlds-del').addEventListener('click', () => {
        const sel = $('#worlds-list');
        if (sel.selectedIndex >= 0) {
            const worlds = Settings.getWorlds();
            Settings.removeWorld(worlds[sel.selectedIndex].name);
            refreshWorldsList();
        }
    });
    $('#worlds-connect').addEventListener('click', () => {
        const sel = $('#worlds-list');
        if (sel.selectedIndex >= 0) {
            const w = Settings.getWorlds()[sel.selectedIndex];
            connectWorld(w.name, w.host, w.port, w.ssl);
            $('#worlds-dialog').close();
        }
    });
    $('#worlds-list').addEventListener('dblclick', () => {
        $('#worlds-connect').click();
    });
    $('#worlds-close').addEventListener('click', () => $('#worlds-dialog').close());

    // World edit dialog
    $('#we-ok').addEventListener('click', () => {
        const world = {
            name: $('#we-name').value.trim(),
            host: $('#we-host').value.trim(),
            port: $('#we-port').value.trim() || '4201',
            character: $('#we-char').value.trim(),
            ssl: $('#we-ssl').checked,
        };
        if (world.name && world.host) {
            Settings.addWorld(world);
            refreshWorldsList();
        }
        $('#world-edit-dialog').close();
    });
    $('#we-cancel').addEventListener('click', () => $('#world-edit-dialog').close());

    // Find dialog
    $('#find-up').addEventListener('click', () => {
        findInScrollback($('#find-text').value, true);
    });
    $('#find-down').addEventListener('click', () => {
        findInScrollback($('#find-text').value, false);
    });
    $('#find-close').addEventListener('click', () => $('#find-dialog').close());

    // Load triggers, hooks, spawns from settings
    const savedTriggers = Settings.get('triggers') || [];
    app.triggerDB.loadFrom(savedTriggers);
    const savedHooks = Settings.get('hooks') || [];
    app.hookDB.loadFrom(savedHooks);
    const savedSpawns = Settings.get('spawns') || [];
    app.spawnDB.loadFrom(savedSpawns);

    // Timer fire callback
    app.timerDB.onFire = (name, command) => {
        const conn = app.connections[app.activeTab];
        if (conn && conn.connected) conn.sendLine(command);
    };

    // Toolbar buttons
    $('#btn-connect').addEventListener('click', showConnectDialog);
    $('#btn-worlds').addEventListener('click', showWorldsDialog);
    $('#btn-disconnect').addEventListener('click', () => {
        if (app.activeTab && app.activeTab !== '(System)') removeTab(app.activeTab);
    });
    $('#btn-find').addEventListener('click', showFindDialog);
    $('#btn-clear').addEventListener('click', () => {
        const output = $('#output');
        output.innerHTML = '';
        if (app.activeTab === '(System)') {
            app._systemLines = [];
        } else {
            const conn = app.connections[app.activeTab];
            if (conn) conn.scrollback = [];
        }
    });

    // Triggers toolbar button
    $('#btn-triggers').addEventListener('click', showTriggersDialog);

    // Trigger manager dialog
    $('#trig-add').addEventListener('click', () => {
        showTrigEditDialog(null);
    });
    $('#trig-edit').addEventListener('click', () => {
        const sel = $('#triggers-list');
        if (sel.selectedIndex >= 0) {
            const trigs = app.triggerDB.list();
            showTrigEditDialog(trigs[sel.selectedIndex]);
        }
    });
    $('#trig-del').addEventListener('click', () => {
        const sel = $('#triggers-list');
        if (sel.selectedIndex >= 0) {
            const trigs = app.triggerDB.list();
            app.triggerDB.remove(trigs[sel.selectedIndex].name);
            Settings.set('triggers', app.triggerDB.toJSON());
            refreshTriggersList();
        }
    });
    $('#trig-close').addEventListener('click', () => $('#triggers-dialog').close());

    // Trigger edit dialog
    $('#te-ok').addEventListener('click', () => {
        const def = {
            name: $('#te-name').value.trim(),
            pattern: $('#te-pattern').value.trim(),
            body: $('#te-body').value.trim(),
            priority: parseInt($('#te-priority').value) || 0,
            shots: parseInt($('#te-shots').value),
            gag: $('#te-gag').checked,
        };
        if (isNaN(def.shots)) def.shots = -1;
        if (def.pattern) {
            app.triggerDB.add(def);
            Settings.set('triggers', app.triggerDB.toJSON());
            refreshTriggersList();
        }
        $('#trig-edit-dialog').close();
    });
    $('#te-cancel').addEventListener('click', () => $('#trig-edit-dialog').close());

    // Input auto-complete: Tab key completes /commands
    input.addEventListener('keydown', (e) => {
        if (e.key === 'Tab') {
            e.preventDefault();
            const val = input.value;
            if (val.startsWith('/') && !val.includes(' ')) {
                const prefix = val.toLowerCase();
                const matches = COMMANDS.filter(c => c.startsWith(prefix));
                if (matches.length === 1) {
                    input.value = matches[0] + ' ';
                } else if (matches.length > 1) {
                    appendLine(app.activeTab, '% ' + matches.join('  '));
                }
            }
        }
    });

    // MCP editor dialog
    const mcpSave = $('#mcp-edit-save');
    const mcpCancel = $('#mcp-edit-cancel');
    if (mcpSave) {
        mcpSave.addEventListener('click', () => {
            const dlg = $('#mcp-editor-dialog');
            const mcp = app.mcpParsers[dlg._tabName];
            if (mcp) {
                mcp.sendEditSet(dlg._reference, dlg._type, $('#mcp-edit-text').value);
                appendLine(dlg._tabName, '% MCP edit saved: ' + dlg._reference);
            }
            dlg.close();
        });
    }
    if (mcpCancel) {
        mcpCancel.addEventListener('click', () => $('#mcp-editor-dialog').close());
    }

    // Periodic status update
    setInterval(updateStatus, 2000);
}

document.addEventListener('DOMContentLoaded', init);
