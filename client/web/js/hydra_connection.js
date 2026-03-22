// hydra_connection.js -- gRPC-Web connection to Hydra proxy.
//
// Implements the same interface as Connection (WebSocket) so main.js
// can use either transport interchangeably.
//
// Uses the grpc-web protocol (HTTP/1.1 POST with binary framing)
// via the Fetch API.  Protobuf messages are encoded/decoded manually
// for the small set of types the browser needs.
'use strict';

// ---- Minimal protobuf encoder/decoder ----
//
// Only supports the subset of proto3 needed by Hydra RPCs:
// string (wire type 2), bool (wire type 0), int32/int64 (wire type 0).

const Proto = {
    // Encode a field: tag + value
    encodeVarint(value) {
        const bytes = [];
        value = value >>> 0;  // ensure unsigned
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
        if (!value) return [];
        const encoded = new TextEncoder().encode(value);
        return [
            ...this.encodeTag(fieldNum, 2),
            ...this.encodeVarint(encoded.length),
            ...encoded
        ];
    },

    encodeBool(fieldNum, value) {
        if (!value) return [];
        return [...this.encodeTag(fieldNum, 0), value ? 1 : 0];
    },

    encodeInt32(fieldNum, value) {
        if (!value) return [];
        return [...this.encodeTag(fieldNum, 0), ...this.encodeVarint(value)];
    },

    // Encode a message object to bytes using a field map.
    // fieldMap: {fieldName: {num, type}}
    encode(obj, fieldMap) {
        const parts = [];
        for (const [name, spec] of Object.entries(fieldMap)) {
            const val = obj[name];
            if (val === undefined || val === null || val === '' || val === 0 || val === false) continue;
            switch (spec.type) {
                case 'string': parts.push(...this.encodeString(spec.num, val)); break;
                case 'bool':   parts.push(...this.encodeBool(spec.num, val)); break;
                case 'int32':  parts.push(...this.encodeInt32(spec.num, val)); break;
                case 'int64':  parts.push(...this.encodeInt32(spec.num, val)); break; // JS safe int
            }
        }
        return new Uint8Array(parts);
    },

    // Decode protobuf bytes to an object.
    // fieldMap: {fieldNum: {name, type}}
    decode(bytes, fieldMap) {
        const result = {};
        let pos = 0;
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

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

            if (wireType === 0) { // varint
                const val = readVarint();
                if (spec) {
                    result[spec.name] = spec.type === 'bool' ? !!val : val;
                }
            } else if (wireType === 2) { // length-delimited
                const len = readVarint();
                const data = bytes.slice(pos, pos + len);
                pos += len;
                if (spec) {
                    if (spec.type === 'string') {
                        result[spec.name] = new TextDecoder().decode(data);
                    } else if (spec.type === 'message') {
                        // Nested message — decode recursively
                        const nested = this.decode(data, spec.fields);
                        if (spec.repeated) {
                            if (!result[spec.name]) result[spec.name] = [];
                            result[spec.name].push(nested);
                        } else {
                            result[spec.name] = nested;
                        }
                    }
                }
            } else if (wireType === 5) { // 32-bit
                pos += 4;
            } else if (wireType === 1) { // 64-bit
                pos += 8;
            }
        }
        return result;
    }
};

// ---- Protobuf field maps for Hydra messages ----

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

// ---- grpc-web frame codec ----

function grpcWebEncodeRequest(protoBytes) {
    // 5-byte header: flag(0) + length(4 big-endian)
    const frame = new Uint8Array(5 + protoBytes.length);
    frame[0] = 0x00;  // not compressed
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
        const payload = view.slice(pos, pos + len);
        pos += len;
        frames.push({flag, payload});
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
    }

    // Base URL for grpc-web HTTP/1.1 POST
    get _baseUrl() {
        const proto = this.ssl ? 'https' : 'http';
        return `${proto}://${this.host}:${this.port}`;
    }

    // Make a unary grpc-web RPC call.
    async _rpc(method, requestBytes, responseDecoder) {
        const body = grpcWebEncodeRequest(requestBytes);
        const headers = {
            'Content-Type': 'application/grpc-web+proto',
            'X-Grpc-Web': '1',
        };
        if (this.sessionId) {
            headers['Authorization'] = this.sessionId;
        }

        const resp = await fetch(`${this._baseUrl}/hydra.HydraService/${method}`, {
            method: 'POST',
            headers,
            body,
        });

        const buf = await resp.arrayBuffer();
        const frames = grpcWebDecodeFrames(buf);

        let result = {};
        for (const frame of frames) {
            if (frame.flag === 0x00 && responseDecoder) {
                result = Proto.decode(frame.payload, responseDecoder);
            } else if (frame.flag === 0x80) {
                const trailers = parseTrailerFrame(frame.payload);
                if (trailers['grpc-status'] && trailers['grpc-status'] !== '0') {
                    result._error = trailers['grpc-message'] || 'RPC failed';
                }
            }
        }
        return result;
    }

    async connect() {
        try {
            // We need credentials — these come from the world config.
            // The caller should set this.username and this.password before calling connect().
            if (!this.username || !this.password) {
                if (this.onLine) this.onLine('% [Hydra] No credentials configured.');
                return false;
            }

            // Authenticate
            const authBytes = Proto.encode(
                {username: this.username, password: this.password},
                AuthRequestFields
            );
            const auth = await this._rpc('Authenticate', authBytes, AuthResponseDecode);

            if (!auth.success) {
                if (this.onLine) this.onLine('% [Hydra] Auth failed: ' + (auth.error || auth._error || 'unknown'));
                return false;
            }

            this.sessionId = auth.session_id;
            this.connected = true;

            if (this.onConnect) this.onConnect();

            // Start output subscription
            this._startSubscribe();

            return true;
        } catch (e) {
            if (this.onLine) this.onLine('% [Hydra] Connection error: ' + e.message);
            return false;
        }
    }

    disconnect() {
        this.connected = false;
        if (this._subscribeAbort) {
            this._subscribeAbort.abort();
            this._subscribeAbort = null;
        }
        if (this.onDisconnect) this.onDisconnect();
    }

    async sendLine(text) {
        if (!this.connected || !this.sessionId) return;
        try {
            const reqBytes = Proto.encode(
                {session_id: this.sessionId, line: text},
                InputRequestFields
            );
            await this._rpc('SendInput', reqBytes, InputResponseDecode);
        } catch (e) {
            if (this.onLine) this.onLine('% [Hydra] Send error: ' + e.message);
        }
    }

    sendNaws(width, height) {
        // NAWS is telnet-specific; grpc-web doesn't need it
    }

    addScrollback(line) {
        this.scrollback.push(line);
        while (this.scrollback.length > this.maxScrollback) {
            this.scrollback.shift();
        }
    }

    // Connect to a specific game via Hydra
    async connectGame(gameName) {
        if (!this.connected || !this.sessionId) return;
        try {
            const reqBytes = Proto.encode(
                {session_id: this.sessionId, game_name: gameName},
                ConnectRequestFields
            );
            const resp = await this._rpc('Connect', reqBytes, ConnectResponseDecode);
            if (resp.success) {
                if (this.onLine) this.onLine('% [Hydra] Connected to ' + gameName + ' (link ' + resp.link_number + ')');
            } else {
                if (this.onLine) this.onLine('% [Hydra] Connect failed: ' + (resp.error || 'unknown'));
            }
        } catch (e) {
            if (this.onLine) this.onLine('% [Hydra] Connect error: ' + e.message);
        }
    }

    // List available games
    async listGames() {
        try {
            const resp = await this._rpc('ListGames', new Uint8Array(0), GameListDecode);
            return resp.games || [];
        } catch (e) {
            return [];
        }
    }

    // ---- Server-streaming Subscribe via chunked fetch ----

    async _startSubscribe() {
        if (!this.sessionId) return;

        const controller = new AbortController();
        this._subscribeAbort = controller;

        const reqBytes = Proto.encode(
            {session_id: this.sessionId},
            SessionRequestFields
        );
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

                // Accumulate bytes
                const newBuf = new Uint8Array(buffer.length + value.length);
                newBuf.set(buffer);
                newBuf.set(value, buffer.length);
                buffer = newBuf;

                // Parse complete frames
                let pos = 0;
                while (pos + 5 <= buffer.length) {
                    const flag = buffer[pos];
                    const len = (buffer[pos+1] << 24) | (buffer[pos+2] << 16)
                              | (buffer[pos+3] << 8)  | buffer[pos+4];
                    if (pos + 5 + len > buffer.length) break;  // incomplete

                    const payload = buffer.slice(pos + 5, pos + 5 + len);
                    pos += 5 + len;

                    if (flag === 0x00) {
                        // Data frame — GameOutput message
                        const output = Proto.decode(payload, GameOutputDecode);
                        if (output.text && this.onLine) {
                            this.addScrollback(output.text);
                            this.onLine(output.text);
                        }
                    }
                    // flag 0x80 = trailer — stream end
                }
                buffer = buffer.slice(pos);
            }
        } catch (e) {
            if (e.name !== 'AbortError' && this.connected) {
                if (this.onLine) this.onLine('% [Hydra] Subscribe stream lost: ' + e.message);
                this.connected = false;
                if (this.onDisconnect) this.onDisconnect();
            }
        }
    }
}
