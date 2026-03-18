// main.js -- Titan web client entry point.
'use strict';

const app = {
    connections: {},    // name -> Connection
    tabs: [],           // [{name, conn, active}]
    activeTab: null,    // name
    history: {},        // name -> [lines]
    historyPos: {},     // name -> int
    savedInput: {},     // name -> string
};

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
                       (tab.active ? ' has-activity' : '');
        el.innerHTML = `<span class="activity-dot"></span>${escapeHtml(tab.name)}` +
                       (tab.name !== '(System)' ? `<span class="close-btn">\u00d7</span>` : '');
        el.addEventListener('click', (e) => {
            if (e.target.classList.contains('close-btn')) removeTab(tab.name);
            else switchTab(tab.name);
        });
        el.addEventListener('auxclick', (e) => {
            if (e.button === 1) removeTab(tab.name);
        });
        bar.appendChild(el);
    }
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
    bar.textContent = s;
}

// -- Connection --

function connectWorld(name, host, port, ssl) {
    addTab(name);
    const conn = new Connection(name, host, port, ssl);
    app.connections[name] = conn;

    conn.onLine = (line) => appendLine(name, line);
    conn.onPrompt = (prompt) => appendLine(name, prompt);
    conn.onConnect = () => {
        appendLine(name, '% Connected to ' + host + ':' + port);
        updateStatus();
    };
    conn.onDisconnect = () => {
        appendLine(name, '% Connection lost.');
        updateStatus();
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
    if (e.ctrlKey && e.key === 'n') { e.preventDefault(); showConnectDialog(); }
    else if (e.ctrlKey && e.key === 'f') { e.preventDefault(); showFindDialog(); }
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

// -- Init --

function init() {
    Settings.load();
    app._systemLines = [];

    // System tab
    addTab('(System)');
    appendLine('(System)', 'Titan for Web');
    appendLine('(System)', 'Press Ctrl+N to connect, or File > Worlds to manage worlds.');

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

    // Periodic status update
    setInterval(updateStatus, 2000);
}

document.addEventListener('DOMContentLoaded', init);
