#!/usr/bin/env node
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = __dirname;

function createStorage() {
    const data = new Map();
    return {
        getItem(key) {
            return data.has(key) ? data.get(key) : null;
        },
        setItem(key, value) {
            data.set(key, String(value));
        },
        removeItem(key) {
            data.delete(key);
        },
        clear() {
            data.clear();
        },
    };
}

function loadScript(relPath, exportNames, extra = {}) {
    const code = fs.readFileSync(path.join(root, relPath), 'utf8');
    const context = {
        console,
        TextEncoder,
        TextDecoder,
        Uint8Array,
        ArrayBuffer,
        AbortController,
        setTimeout(fn) {
            fn();
            return 1;
        },
        clearTimeout() {},
        setInterval() {
            return 1;
        },
        clearInterval() {},
        ...extra,
    };
    context.globalThis = context;
    vm.createContext(context);
    const exportSrc = '\n;globalThis.__exports = {' + exportNames.join(', ') + '};\n';
    vm.runInContext(code + exportSrc, context, { filename: relPath });
    return { context, exports: context.__exports };
}

function encodeGrpcWebResponse(payload) {
    const trailerText = 'grpc-status: 0\r\n';
    const trailer = new TextEncoder().encode(trailerText);
    const out = new Uint8Array(5 + payload.length + 5 + trailer.length);
    out[0] = 0x00;
    out[1] = (payload.length >>> 24) & 0xFF;
    out[2] = (payload.length >>> 16) & 0xFF;
    out[3] = (payload.length >>> 8) & 0xFF;
    out[4] = payload.length & 0xFF;
    out.set(payload, 5);
    const pos = 5 + payload.length;
    out[pos] = 0x80;
    out[pos + 1] = (trailer.length >>> 24) & 0xFF;
    out[pos + 2] = (trailer.length >>> 16) & 0xFF;
    out[pos + 3] = (trailer.length >>> 8) & 0xFF;
    out[pos + 4] = trailer.length & 0xFF;
    out.set(trailer, pos + 5);
    return out.buffer;
}

async function flush() {
    for (let i = 0; i < 5; i++) {
        await Promise.resolve();
    }
}

function makeHydraEnv() {
    const sessionStorage = createStorage();
    const localStorage = createStorage();
    class FakeWebSocket {
        static instances = [];
        static OPEN = 1;

        constructor(url, protocols) {
            this.url = url;
            this.protocols = protocols;
            this.readyState = 0;
            this.binaryType = 'arraybuffer';
            this.sent = [];
            FakeWebSocket.instances.push(this);
        }

        send(data) {
            this.sent.push(data);
        }

        open() {
            this.readyState = FakeWebSocket.OPEN;
            if (this.onopen) this.onopen();
        }

        emitMessage(data) {
            if (this.onmessage) this.onmessage({ data });
        }

        close() {
            this.readyState = 3;
            if (this.onclose) this.onclose();
        }
    }

    const loaded = loadScript(
        'js/hydra_connection.js',
        ['HydraConnection', 'Proto', 'AuthRequestFields', 'SessionRequestFields',
         'ClientMessageFields', 'grpcWebEncodeRequest'],
        {
            window: { innerWidth: 800, innerHeight: 600 },
            sessionStorage,
            localStorage,
            WebSocket: FakeWebSocket,
            fetch: async () => { throw new Error('fetch not configured'); },
        }
    );
    return {
        ...loaded,
        sessionStorage,
        localStorage,
        FakeWebSocket,
    };
}

async function testSettingsPasswordMigration() {
    const localStorage = createStorage();
    const sessionStorage = createStorage();
    localStorage.setItem('titan_settings', JSON.stringify({
        worlds: [{ name: 'Alpha', host: 'mux.example', password: 'secret' }]
    }));

    const { exports } = loadScript('js/settings.js', ['Settings'], {
        localStorage,
        sessionStorage,
    });
    const Settings = exports.Settings;
    Settings.load();
    const worlds = Settings.getWorlds();
    assert.strictEqual(worlds[0].password, 'secret');
    assert.ok(!JSON.parse(localStorage.getItem('titan_settings')).worlds[0].password);
    assert.strictEqual(sessionStorage.getItem('titan_world_password:Alpha'), 'secret');
}

async function testHydraFreshAuthAndPromptHandling() {
    const env = makeHydraEnv();
    const { HydraConnection, Proto } = env.exports;
    const calls = [];
    const AuthResponseFields = {
        success: { num: 1, type: 'bool' },
        session_id: { num: 2, type: 'string' },
        error: { num: 3, type: 'string' },
    };
    const GameOutputFields = {
        text: { num: 1, type: 'string' },
        end_of_record: { num: 5, type: 'bool' },
    };
    const ServerMessageFields = {
        game_output: { num: 1, type: 'message', fields: GameOutputFields },
    };
    const ClientMessageDecode = {
        4: { name: 'preferences', type: 'message', fields: {
            1: { name: 'color_format', type: 'int32' },
            2: { name: 'terminal_width', type: 'int32' },
            3: { name: 'terminal_height', type: 'int32' },
            4: { name: 'terminal_type', type: 'string' },
            5: { name: 'session_id', type: 'string' },
        } },
    };

    env.context.fetch = async (url) => {
        calls.push(url);
        if (url.endsWith('/Authenticate')) {
            return {
                ok: true,
                status: 200,
                statusText: 'OK',
                arrayBuffer: async () => encodeGrpcWebResponse(Proto.encode({
                    success: true,
                    session_id: 'sess-1234',
                }, AuthResponseFields)),
            };
        }
        throw new Error('unexpected fetch ' + url);
    };

    const conn = new HydraConnection('World', 'hydra.example', 4201, true);
    conn.username = 'alice';
    conn.password = 'pw';
    const lines = [];
    const prompts = [];
    let connected = false;
    conn.onLine = line => lines.push(line);
    conn.onPrompt = prompt => prompts.push(prompt);
    conn.onConnect = () => { connected = true; };

    const ok = await conn.connect();
    assert.strictEqual(ok, true);
    assert.strictEqual(connected, true);
    assert.strictEqual(env.sessionStorage.getItem('hydra_session_World'), 'sess-1234');
    assert.strictEqual(env.FakeWebSocket.instances.length, 1);

    const ws = env.FakeWebSocket.instances[0];
    ws.open();
    const sent = Proto.decode(new Uint8Array(ws.sent[0]), ClientMessageDecode);
    assert.strictEqual(sent.preferences.session_id, 'sess-1234');
    assert.strictEqual(sent.preferences.terminal_type, 'Hydra-Web');

    const msg = Proto.encode({
        game_output: { text: 'hello world\nlook', end_of_record: true }
    }, ServerMessageFields);
    ws.emitMessage(msg);
    assert.deepStrictEqual(lines, ['hello world']);
    assert.deepStrictEqual(prompts, ['look']);
}

async function testHydraSessionResumeSkipsAuthenticate() {
    const env = makeHydraEnv();
    const { HydraConnection, Proto } = env.exports;
    const calls = [];
    const GetSessionResponseFields = {
        session_id: { num: 1, type: 'string' },
        username: { num: 2, type: 'string' },
        state: { num: 6, type: 'int32' },
    };

    env.sessionStorage.setItem('hydra_session_World', 'resume-1');
    env.context.fetch = async (url) => {
        calls.push(url);
        if (url.endsWith('/GetSession')) {
            return {
                ok: true,
                status: 200,
                statusText: 'OK',
                arrayBuffer: async () => encodeGrpcWebResponse(Proto.encode({
                    session_id: 'resume-1',
                    username: 'alice',
                    state: 1,
                }, GetSessionResponseFields)),
            };
        }
        throw new Error('unexpected fetch ' + url);
    };

    const conn = new HydraConnection('World', 'hydra.example', 4201, true);
    conn.username = 'alice';
    conn.password = 'pw';
    const ok = await conn.connect();
    assert.strictEqual(ok, true);
    assert.strictEqual(conn.sessionId, 'resume-1');
    assert.strictEqual(calls.length, 1);
    assert.ok(calls[0].endsWith('/GetSession'));
}

async function testHydraFallbackSubscribePath() {
    const env = makeHydraEnv();
    const { HydraConnection, Proto } = env.exports;
    const AuthResponseFields = {
        success: { num: 1, type: 'bool' },
        session_id: { num: 2, type: 'string' },
        error: { num: 3, type: 'string' },
    };
    const GameOutputFields = {
        text: { num: 1, type: 'string' },
        end_of_record: { num: 5, type: 'bool' },
    };
    const calls = [];

    env.context.fetch = async (url) => {
        calls.push(url);
        if (url.endsWith('/Authenticate')) {
            return {
                ok: true,
                status: 200,
                statusText: 'OK',
                arrayBuffer: async () => encodeGrpcWebResponse(Proto.encode({
                    success: true,
                    session_id: 'sess-fallback',
                }, AuthResponseFields)),
            };
        }
        if (url.endsWith('/Subscribe')) {
            const payload = Proto.encode({
                text: 'stream line\nprompt',
                end_of_record: true,
            }, GameOutputFields);
            const frame = new Uint8Array(5 + payload.length);
            frame[0] = 0x00;
            frame[1] = (payload.length >>> 24) & 0xFF;
            frame[2] = (payload.length >>> 16) & 0xFF;
            frame[3] = (payload.length >>> 8) & 0xFF;
            frame[4] = payload.length & 0xFF;
            frame.set(payload, 5);
            let readCount = 0;
            return {
                ok: true,
                status: 200,
                statusText: 'OK',
                body: {
                    getReader() {
                        return {
                            async read() {
                                readCount++;
                                if (readCount === 1) {
                                    return { done: false, value: frame };
                                }
                                return new Promise(() => {});
                            }
                        };
                    }
                }
            };
        }
        throw new Error('unexpected fetch ' + url);
    };

    const conn = new HydraConnection('World', 'hydra.example', 4201, true);
    conn.username = 'alice';
    conn.password = 'pw';
    const lines = [];
    const prompts = [];
    conn.onLine = line => lines.push(line);
    conn.onPrompt = prompt => prompts.push(prompt);
    await conn.connect();
    const ws = env.FakeWebSocket.instances[0];
    ws.close();
    await flush();
    assert.ok(calls.some(url => url.endsWith('/Subscribe')));
    assert.ok(lines.includes('% [Hydra] WebSocket unavailable, falling back to gRPC-Web stream.'));
    assert.ok(lines.includes('stream line'));
    assert.deepStrictEqual(prompts, ['prompt']);
}

async function testHydraReconnectCreatesNewWebSocket() {
    const env = makeHydraEnv();
    const { HydraConnection, Proto } = env.exports;
    const AuthResponseFields = {
        success: { num: 1, type: 'bool' },
        session_id: { num: 2, type: 'string' },
        error: { num: 3, type: 'string' },
    };

    env.context.fetch = async (url) => {
        if (url.endsWith('/Authenticate')) {
            return {
                ok: true,
                status: 200,
                statusText: 'OK',
                arrayBuffer: async () => encodeGrpcWebResponse(Proto.encode({
                    success: true,
                    session_id: 'sess-reconnect',
                }, AuthResponseFields)),
            };
        }
        throw new Error('unexpected fetch ' + url);
    };

    const conn = new HydraConnection('World', 'hydra.example', 4201, true);
    conn.username = 'alice';
    conn.password = 'pw';
    const lines = [];
    conn.onLine = line => lines.push(line);
    await conn.connect();
    const firstWs = env.FakeWebSocket.instances[0];
    firstWs.open();
    firstWs.close();
    await flush();
    assert.ok(lines.some(line => line.includes('Stream lost, reconnecting')));
    assert.strictEqual(env.FakeWebSocket.instances.length, 2);
}

async function main() {
    const tests = [
        testSettingsPasswordMigration,
        testHydraFreshAuthAndPromptHandling,
        testHydraSessionResumeSkipsAuthenticate,
        testHydraFallbackSubscribePath,
        testHydraReconnectCreatesNewWebSocket,
    ];

    for (const fn of tests) {
        await fn();
        console.log('PASS', fn.name);
    }
}

main().catch(err => {
    console.error('FAIL', err && err.stack ? err.stack : err);
    process.exit(1);
});
