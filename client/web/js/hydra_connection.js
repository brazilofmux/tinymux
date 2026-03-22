// hydra_connection.js -- gRPC-Web connection to Hydra proxy.
//
// Implements the same interface as Connection (WebSocket) so main.js
// can use either transport interchangeably.
//
// Hydra commands intercepted from user input:
//   /hconnect <game>     — connect to a game via Hydra
//   /hswitch <link#>     — switch active link
//   /hdisconnect <link#> — disconnect a specific link
//   /hlinks              — list active links
//   /hgames              — list available games
//   /hscroll [n]         — fetch scroll-back from server
//   /hhelp               — show Hydra commands
//
// All other input is forwarded to the active game link via SendInput.
'use strict';

// ---- Minimal protobuf encoder/decoder ----

const Proto = {
    encodeVarint(value) {
        const bytes = [];
        value = value >>> 0;
        while (value > 0x7F) {
            bytes.push((value & 0x7F) | 0x80);
            value >>>= 7;
        }
        bytes.push(value & 0x7F);
        return bytes;
    },

    encodeTag(fieldNum, wireType) {
        return this.encodeVarint((fieldNum << 3) | wireType);
    },

    encodeString(fieldNum, value) {
        if (value === undefined || value === null || value === '') return [];
        const encoded = new TextEncoder().encode(value);
        return [
            ...this.encodeTag(fieldNum, 2),
            ...this.encodeVarint(encoded.length),
            ...encoded
        ];
    },

    encodeBool(fieldNum, value) {
        // Proto3: bool defaults to false; only encode if true
        if (!value) return [];
        return [...this.encodeTag(fieldNum, 0), 1];
    },

    encodeInt32(fieldNum, value) {
        // Proto3: int32 defaults to 0; encode non-zero values
        // Also encode 0 if explicitly requested (handled by caller)
        if (value === undefined || value === null) return [];
        if (value === 0) return [];  // proto3 default
        return [...this.encodeTag(fieldNum, 0), ...this.encodeVarint(value)];
    },

    // Encode with explicit support for "always encode" fields
    encodeInt32Always(fieldNum, value) {
        if (value === undefined || value === null) return [];
        return [...this.encodeTag(fieldNum, 0), ...this.encodeVarint(value)];
    },

    encode(obj, fieldMap) {
        const parts = [];
        for (const [name, spec] of Object.entries(fieldMap)) {
            const val = obj[name];
            if (val === undefined || val === null) continue;
            switch (spec.type) {
                case 'string':
                    if (val !== '') parts.push(...this.encodeString(spec.num, val));
                    break;
                case 'bool':
                    if (val) parts.push(...this.encodeBool(spec.num, val));
                    break;
                case 'int32':
                case 'int64':
                    if (val !== 0) parts.push(...this.encodeInt32(spec.num, val));
                    break;
                case 'enum':
                    // Enums: encode even if 0 when explicitly set
                    if (val !== undefined) parts.push(...this.encodeInt32Always(spec.num, val));
                    break;
            }
        }
        return new Uint8Array(parts);
    },

    decode(bytes, fieldMap) {
        const result = {};
        let pos = 0;

        function readVarint() {
            let value = 0, shift = 0;
            while (pos < bytes.length) {
                const b = bytes[pos++];
                value |= (b & 0x7F) << shift;
                if (!(b & 0x80)) return value >>> 0;
                shift += 7;
            }
            return value >>> 0;
        }

        while (pos < bytes.length) {
            const tag = readVarint();
            const fieldNum = tag >>> 3;
            const wireType = tag & 0x07;
            const spec = fieldMap[fieldNum];

            if (wireType === 0) {
                const val = readVarint();
                if (spec) {
                    result[spec.name] = spec.type === 'bool' ? !!val : val;
                }
            } else if (wireType === 2) {
                const len = readVarint();
                const data = bytes.slice(pos, pos + len);
                pos += len;
                if (spec) {
                    if (spec.type === 'string') {
                        result[spec.name] = new TextDecoder().decode(data);
                    } else if (spec.type === 'message') {
                        const nested = Proto.decode(data, spec.fields);
                        if (spec.repeated) {
                            if (!result[spec.name]) result[spec.name] = [];
                            result[spec.name].push(nested);
                        } else {
                            result[spec.name] = nested;
                        }
                    }
                }
            } else if (wireType === 5) {
                pos += 4;
            } else if (wireType === 1) {
                pos += 8;
            }
        }
        return result;
    }
};

// ---- Protobuf field maps ----

const AuthRequestFields = {
    username: {num: 1, type: 'string'},
    password: {num: 2, type: 'string'},
};
const AuthResponseDecode = {
    1: {name: 'success', type: 'bool'},
    2: {name: 'session_id', type: 'string'},
    3: {name: 'error', type: 'string'},
};
const InputRequestFields = {
    session_id: {num: 1, type: 'string'},
    line:       {num: 2, type: 'string'},
};
const InputResponseDecode = {
    1: {name: 'success', type: 'bool'},
    2: {name: 'error', type: 'string'},
};
const SessionRequestFields = {
    session_id: {num: 1, type: 'string'},
};
const GameInfoDecode = {
    1: {name: 'name', type: 'string'},
    2: {name: 'host', type: 'string'},
    3: {name: 'port', type: 'int32'},
    4: {name: 'type', type: 'int32'},
    5: {name: 'autostart', type: 'bool'},
};
const GameListDecode = {
    1: {name: 'games', type: 'message', fields: GameInfoDecode, repeated: true},
};
const GameOutputDecode = {
    1: {name: 'text', type: 'string'},
    2: {name: 'source', type: 'string'},
    3: {name: 'timestamp', type: 'int64'},
    4: {name: 'link_number', type: 'int32'},
};
const ConnectRequestFields = {
    session_id: {num: 1, type: 'string'},
    game_name:  {num: 2, type: 'string'},
};
const ConnectResponseDecode = {
    1: {name: 'success', type: 'bool'},
    2: {name: 'link_number', type: 'int32'},
    3: {name: 'error', type: 'string'},
};
const SwitchRequestFields = {
    session_id:  {num: 1, type: 'string'},
    link_number: {num: 2, type: 'int32'},
};
const SwitchResponseDecode = {
    1: {name: 'success', type: 'bool'},
    2: {name: 'error', type: 'string'},
};
const DisconnectRequestFields = {
    session_id:  {num: 1, type: 'string'},
    link_number: {num: 2, type: 'int32'},
};
const DisconnectResponseDecode = {
    1: {name: 'success', type: 'bool'},
    2: {name: 'error', type: 'string'},
};
const LinkInfoDecode = {
    1: {name: 'number', type: 'int32'},
    2: {name: 'game_name', type: 'string'},
    3: {name: 'state', type: 'int32'},
    4: {name: 'character', type: 'string'},
    5: {name: 'active', type: 'bool'},
};
const LinkListDecode = {
    1: {name: 'links', type: 'message', fields: LinkInfoDecode, repeated: true},
};
const ScrollBackRequestFields = {
    session_id: {num: 1, type: 'string'},
    max_lines:  {num: 2, type: 'int32'},
};
const ScrollBackResponseDecode = {
    1: {name: 'lines', type: 'message', fields: GameOutputDecode, repeated: true},
};

const LINK_STATE_NAMES = {
    0: 'unknown', 1: 'connecting', 2: 'tls', 3: 'negotiating',
    4: 'logging-in', 5: 'active', 6: 'reconnecting', 7: 'suspended', 8: 'dead',
};

// ---- grpc-web frame codec ----

function grpcWebEncodeRequest(protoBytes) {
    const frame = new Uint8Array(5 + protoBytes.length);
    frame[0] = 0x00;
    const len = protoBytes.length;
    frame[1] = (len >>> 24) & 0xFF;
    frame[2] = (len >>> 16) & 0xFF;
    frame[3] = (len >>>  8) & 0xFF;
    frame[4] = (len       ) & 0xFF;
    frame.set(protoBytes, 5);
    return frame;
}

function grpcWebDecodeFrames(buffer) {
    const frames = [];
    const view = new Uint8Array(buffer);
    let pos = 0;
    while (pos + 5 <= view.length) {
        const flag = view[pos];
        const len = (view[pos+1] << 24) | (view[pos+2] << 16) | (view[pos+3] << 8) | view[pos+4];
        pos += 5;
        if (pos + len > view.length) break;
        frames.push({flag, payload: view.slice(pos, pos + len)});
        pos += len;
    }
    return frames;
}

function parseTrailerFrame(payload) {
    const text = new TextDecoder().decode(payload);
    const trailers = {};
    for (const line of text.split('\r\n')) {
        const colon = line.indexOf(':');
        if (colon > 0) {
            trailers[line.substring(0, colon).trim()] = line.substring(colon + 1).trim();
        }
    }
    return trailers;
}

// ---- HydraConnection class ----

const MAX_RECONNECT_ATTEMPTS = 5;
const RECONNECT_DELAY_MS = 3000;

class HydraConnection {
    constructor(name, host, port, ssl) {
        this.name = name;
        this.host = host;
        this.port = port;
        this.ssl = ssl;
        this.connected = false;
        this.sessionId = null;
        this.onLine = null;
        this.onPrompt = null;
        this.onConnect = null;
        this.onDisconnect = null;
        this.scrollback = [];
        this.maxScrollback = 20000;
        this._subscribeAbort = null;
        this._reconnecting = false;
    }

    get _baseUrl() {
        const proto = this.ssl ? 'https' : 'http';
        return `${proto}://${this.host}:${this.port}`;
    }

    _emit(line) {
        if (this.onLine) this.onLine(line);
    }

    async _rpc(method, requestBytes, responseDecoder) {
        const body = grpcWebEncodeRequest(requestBytes);
        const headers = {
            'Content-Type': 'application/grpc-web+proto',
            'X-Grpc-Web': '1',
        };
        if (this.sessionId) headers['Authorization'] = this.sessionId;

        const resp = await fetch(`${this._baseUrl}/hydra.HydraService/${method}`, {
            method: 'POST', headers, body,
        });

        if (!resp.ok) {
            throw new Error(`HTTP ${resp.status} ${resp.statusText}`);
        }

        const buf = await resp.arrayBuffer();
        const frames = grpcWebDecodeFrames(buf);

        let result = {};
        for (const frame of frames) {
            if (frame.flag === 0x00 && responseDecoder) {
                result = Proto.decode(frame.payload, responseDecoder);
            } else if (frame.flag === 0x80) {
                const trailers = parseTrailerFrame(frame.payload);
                if (trailers['grpc-status'] && trailers['grpc-status'] !== '0') {
                    result._error = trailers['grpc-message'] || 'RPC failed (status ' + trailers['grpc-status'] + ')';
                }
            }
        }
        return result;
    }

    // ---- Connect / Disconnect ----

    async connect() {
        try {
            if (!this.username || !this.password) {
                this._emit('% [Hydra] No credentials configured.');
                return false;
            }

            const authBytes = Proto.encode(
                {username: this.username, password: this.password},
                AuthRequestFields
            );
            const auth = await this._rpc('Authenticate', authBytes, AuthResponseDecode);

            if (!auth.success) {
                this._emit('% [Hydra] Auth failed: ' + (auth.error || auth._error || 'unknown'));
                return false;
            }

            this.sessionId = auth.session_id;
            this.connected = true;

            if (this.onConnect) this.onConnect();
            this._startSubscribe();
            return true;
        } catch (e) {
            this._emit('% [Hydra] Connection error: ' + e.message);
            return false;
        }
    }

    disconnect() {
        this.connected = false;
        this._reconnecting = false;
        if (this._subscribeAbort) {
            this._subscribeAbort.abort();
            this._subscribeAbort = null;
        }
        if (this.onDisconnect) this.onDisconnect();
    }

    // ---- Send with command interception ----

    async sendLine(text) {
        if (!this.connected || !this.sessionId) return;

        // Intercept Hydra commands
        const lower = text.toLowerCase().trim();
        if (lower.startsWith('/h')) {
            if (lower.startsWith('/hconnect ')) {
                await this._cmdConnect(text.substring(10).trim());
            } else if (lower.startsWith('/hswitch ')) {
                await this._cmdSwitch(text.substring(9).trim());
            } else if (lower.startsWith('/hdisconnect ')) {
                await this._cmdDisconnect(text.substring(13).trim());
            } else if (lower === '/hlinks') {
                await this._cmdLinks();
            } else if (lower === '/hgames') {
                await this._cmdGames();
            } else if (lower.startsWith('/hscroll')) {
                await this._cmdScroll(text.substring(8).trim());
            } else if (lower === '/hhelp') {
                this._emit('% [Hydra] Commands:');
                this._emit('%   /hconnect <game>     - connect to a game');
                this._emit('%   /hswitch <link#>     - switch active link');
                this._emit('%   /hdisconnect <link#> - disconnect a link');
                this._emit('%   /hlinks              - list active links');
                this._emit('%   /hgames              - list available games');
                this._emit('%   /hscroll [n]         - fetch server scroll-back');
                this._emit('%   /hhelp               - this help');
            } else {
                // Unknown /h command — send as input
                await this._sendInput(text);
            }
            return;
        }

        await this._sendInput(text);
    }

    async _sendInput(text) {
        try {
            const reqBytes = Proto.encode(
                {session_id: this.sessionId, line: text},
                InputRequestFields
            );
            const resp = await this._rpc('SendInput', reqBytes, InputResponseDecode);
            if (resp._error) this._emit('% [Hydra] ' + resp._error);
        } catch (e) {
            this._emit('% [Hydra] Send error: ' + e.message);
        }
    }

    sendNaws(width, height) { /* not applicable */ }

    addScrollback(line) {
        this.scrollback.push(line);
        while (this.scrollback.length > this.maxScrollback) this.scrollback.shift();
    }

    // ---- Hydra command handlers ----

    async _cmdConnect(gameName) {
        if (!gameName) { this._emit('% [Hydra] Usage: /hconnect <game>'); return; }
        await this.connectGame(gameName);
    }

    async _cmdSwitch(args) {
        const num = parseInt(args);
        if (!num || num < 1) { this._emit('% [Hydra] Usage: /hswitch <link#>'); return; }
        try {
            const reqBytes = Proto.encode(
                {session_id: this.sessionId, link_number: num},
                SwitchRequestFields
            );
            const resp = await this._rpc('SwitchLink', reqBytes, SwitchResponseDecode);
            if (resp.success) {
                this._emit('% [Hydra] Switched to link ' + num);
            } else {
                this._emit('% [Hydra] Switch failed: ' + (resp.error || resp._error || 'unknown'));
            }
        } catch (e) { this._emit('% [Hydra] Switch error: ' + e.message); }
    }

    async _cmdDisconnect(args) {
        const num = parseInt(args);
        if (!num || num < 1) { this._emit('% [Hydra] Usage: /hdisconnect <link#>'); return; }
        try {
            const reqBytes = Proto.encode(
                {session_id: this.sessionId, link_number: num},
                DisconnectRequestFields
            );
            const resp = await this._rpc('DisconnectLink', reqBytes, DisconnectResponseDecode);
            if (resp.success) {
                this._emit('% [Hydra] Link ' + num + ' disconnected');
            } else {
                this._emit('% [Hydra] Disconnect failed: ' + (resp.error || resp._error || 'unknown'));
            }
        } catch (e) { this._emit('% [Hydra] Disconnect error: ' + e.message); }
    }

    async _cmdLinks() {
        try {
            const reqBytes = Proto.encode(
                {session_id: this.sessionId}, SessionRequestFields);
            const resp = await this._rpc('ListLinks', reqBytes, LinkListDecode);
            if (!resp.links || resp.links.length === 0) {
                this._emit('% [Hydra] No active links.');
                return;
            }
            for (const li of resp.links) {
                const marker = li.active ? '*' : ' ';
                let line = `  [${marker}${li.number}] ${li.game_name}`;
                if (li.character) line += ` (${li.character})`;
                line += ' — ' + (LINK_STATE_NAMES[li.state] || 'unknown');
                this._emit(line);
            }
        } catch (e) { this._emit('% [Hydra] Links error: ' + e.message); }
    }

    async _cmdGames() {
        try {
            const games = await this.listGames();
            this._emit('% [Hydra] Available games:');
            for (const g of games) {
                let line = '  ' + g.name + ' (' + g.host + ':' + g.port + ')';
                if (g.type === 1) line += ' [local]';
                this._emit(line);
            }
        } catch (e) { this._emit('% [Hydra] Games error: ' + e.message); }
    }

    async _cmdScroll(args) {
        const n = parseInt(args) || 50;
        try {
            const reqBytes = Proto.encode(
                {session_id: this.sessionId, max_lines: n},
                ScrollBackRequestFields
            );
            const resp = await this._rpc('GetScrollBack', reqBytes, ScrollBackResponseDecode);
            const lines = resp.lines || [];
            this._emit('-- Server scroll-back (' + lines.length + ' lines) --');
            for (const l of lines) this._emit(l.text || '');
            this._emit('-- End server scroll-back --');
        } catch (e) { this._emit('% [Hydra] Scroll error: ' + e.message); }
    }

    async connectGame(gameName) {
        if (!this.connected || !this.sessionId) return;
        try {
            const reqBytes = Proto.encode(
                {session_id: this.sessionId, game_name: gameName},
                ConnectRequestFields
            );
            const resp = await this._rpc('Connect', reqBytes, ConnectResponseDecode);
            if (resp.success) {
                this._emit('% [Hydra] Connected to ' + gameName + ' (link ' + resp.link_number + ')');
            } else {
                this._emit('% [Hydra] Connect failed: ' + (resp.error || resp._error || 'unknown'));
            }
        } catch (e) {
            this._emit('% [Hydra] Connect error: ' + e.message);
        }
    }

    async listGames() {
        try {
            const resp = await this._rpc('ListGames', new Uint8Array(0), GameListDecode);
            return resp.games || [];
        } catch (e) {
            return [];
        }
    }

    // ---- Subscribe with reconnect ----

    async _startSubscribe() {
        if (!this.sessionId) return;

        const controller = new AbortController();
        this._subscribeAbort = controller;

        const reqBytes = Proto.encode(
            {session_id: this.sessionId}, SessionRequestFields);
        const body = grpcWebEncodeRequest(reqBytes);

        try {
            const resp = await fetch(`${this._baseUrl}/hydra.HydraService/Subscribe`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/grpc-web+proto',
                    'X-Grpc-Web': '1',
                    'Authorization': this.sessionId,
                },
                body,
                signal: controller.signal,
            });

            const reader = resp.body.getReader();
            let buffer = new Uint8Array(0);

            while (this.connected) {
                const {done, value} = await reader.read();
                if (done) break;

                const newBuf = new Uint8Array(buffer.length + value.length);
                newBuf.set(buffer);
                newBuf.set(value, buffer.length);
                buffer = newBuf;

                let pos = 0;
                while (pos + 5 <= buffer.length) {
                    const flag = buffer[pos];
                    const len = (buffer[pos+1] << 24) | (buffer[pos+2] << 16)
                              | (buffer[pos+3] << 8)  | buffer[pos+4];
                    if (pos + 5 + len > buffer.length) break;

                    const payload = buffer.slice(pos + 5, pos + 5 + len);
                    pos += 5 + len;

                    if (flag === 0x00) {
                        const output = Proto.decode(payload, GameOutputDecode);
                        if (output.text) {
                            this.addScrollback(output.text);
                            this._emit(output.text);
                        }
                    }
                }
                buffer = buffer.slice(pos);
            }
        } catch (e) {
            if (e.name === 'AbortError') return;  // intentional disconnect
        }

        // Stream ended — attempt reconnect if still connected
        if (this.connected && !this._reconnecting) {
            this._attemptReconnect();
        }
    }

    async _attemptReconnect() {
        if (this._reconnecting) return;
        this._reconnecting = true;

        for (let attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS; attempt++) {
            this._emit('% [Hydra] Stream lost, reconnecting (' + attempt + '/' + MAX_RECONNECT_ATTEMPTS + ')...');
            await new Promise(r => setTimeout(r, RECONNECT_DELAY_MS));

            if (!this.connected || !this.sessionId) break;

            try {
                this._reconnecting = false;
                await this._startSubscribe();
                return;  // reconnected successfully (will loop in _startSubscribe)
            } catch (e) {
                // retry
            }
        }

        this._reconnecting = false;
        this._emit('% [Hydra] Reconnect failed after ' + MAX_RECONNECT_ATTEMPTS + ' attempts');
        this.connected = false;
        if (this.onDisconnect) this.onDisconnect();
    }
}
