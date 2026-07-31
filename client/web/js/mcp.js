// mcp.js -- MCP 2.1 protocol parser for the web client.
'use strict';

// #1889: bound multiline reassembly so a hostile game cannot grow the
// browser heap without bound via unterminated #$#* / unique _data-tag floods.
const MCP_MAX_PENDING_MESSAGES = 32;
const MCP_MAX_MESSAGE_BYTES = 256 * 1024;   // per reassembled message
const MCP_MAX_TOTAL_PENDING_BYTES = 1024 * 1024;

class McpParser {
    constructor() {
        this.sessionKey = null;
        this.negotiated = false;
        this.pending = {};          // dataTag -> McpMessage
        this._pendingBytes = 0;     // sum of approx attr sizes in pending
        this.serverPackages = {};
        this.tagCounter = 0;
        this.onEditRequest = null;  // callback(reference, name, type, content)
        this.sendRaw = null;        // callback(string)
        this.onDiagnostic = null;   // optional callback(string)
    }

    // Process a line. Returns true if MCP (should be hidden from display).
    processLine(line) {
        if (!line.startsWith('#$#')) return false;
        if (line.startsWith('#$#*')) this._handleContinuation(line);
        else if (line.startsWith('#$#:')) this._handleEnd(line);
        else this._handleMessage(line);
        return true;
    }

    _diag(msg) {
        if (this.onDiagnostic) this.onDiagnostic(msg);
        else if (typeof console !== 'undefined' && console.warn) {
            console.warn(msg);
        }
    }

    _attrBytes(msg) {
        let n = 0;
        for (const k of Object.keys(msg.attrs)) {
            n += k.length + String(msg.attrs[k]).length;
        }
        return n;
    }

    _dropPending(tag, reason) {
        const msg = this.pending[tag];
        if (!msg) return;
        this._pendingBytes -= (msg._byteSize || 0);
        if (this._pendingBytes < 0) this._pendingBytes = 0;
        delete this.pending[tag];
        this._diag('MCP: dropped pending multiline tag=' + tag + ' (' + reason + ')');
    }

    // Evict oldest pending entries (insertion order) until under caps.
    _evictForRoom(needBytes) {
        const tags = Object.keys(this.pending);
        while (tags.length > 0
               && (tags.length >= MCP_MAX_PENDING_MESSAGES
                   || this._pendingBytes + needBytes > MCP_MAX_TOTAL_PENDING_BYTES)) {
            const oldest = tags.shift();
            this._dropPending(oldest, 'evicted for capacity');
        }
    }

    _tokenize(input) {
        const tokens = [];
        let i = 0;
        while (i < input.length) {
            while (i < input.length && input[i] === ' ') i++;
            if (i >= input.length) break;
            if (input[i] === '"') {
                i++;
                let s = '';
                while (i < input.length && input[i] !== '"') {
                    if (input[i] === '\\' && i + 1 < input.length) { i++; s += input[i]; }
                    else s += input[i];
                    i++;
                }
                if (i < input.length) i++;
                tokens.push(s);
            } else {
                const start = i;
                while (i < input.length && input[i] !== ' ') i++;
                tokens.push(input.slice(start, i));
            }
        }
        return tokens;
    }

    _handleMessage(line) {
        const body = line.slice(3); // remove #$#
        const tokens = this._tokenize(body);
        if (tokens.length < 2) return;
        const messageName = tokens[0], authKey = tokens[1];
        if (messageName !== 'mcp' && this.sessionKey && authKey !== this.sessionKey) return;

        const msg = {
            messageName,
            authKey,
            attrs: {},
            multilineKeys: new Set(),
            dataTag: null,
            _byteSize: 0,
        };
        for (let i = 2; i < tokens.length; i++) {
            const tok = tokens[i];
            if (tok.endsWith(':')) {
                const key = tok.slice(0, -1);
                const val = i + 1 < tokens.length ? tokens[i + 1] : '';
                if (key === '_data-tag') msg.dataTag = val;
                else if (key.endsWith('*')) msg.multilineKeys.add(key.slice(0, -1));
                else msg.attrs[key] = val;
                i++;
            }
        }
        if (msg.dataTag && msg.multilineKeys.size) {
            // Replacing an existing tag: free prior size first.
            if (this.pending[msg.dataTag]) {
                this._dropPending(msg.dataTag, 'replaced');
            }
            msg._byteSize = this._attrBytes(msg);
            if (msg._byteSize > MCP_MAX_MESSAGE_BYTES) {
                this._diag('MCP: multiline start exceeds per-message limit, ignored tag='
                    + msg.dataTag);
                return;
            }
            this._evictForRoom(msg._byteSize);
            // If still no room after eviction, drop the new message.
            if (Object.keys(this.pending).length >= MCP_MAX_PENDING_MESSAGES
                || this._pendingBytes + msg._byteSize > MCP_MAX_TOTAL_PENDING_BYTES) {
                this._diag('MCP: pending capacity full, ignored tag=' + msg.dataTag);
                return;
            }
            this.pending[msg.dataTag] = msg;
            this._pendingBytes += msg._byteSize;
        } else {
            this._dispatch(msg);
        }
    }

    _handleContinuation(line) {
        const body = line.slice(5); // remove "#$#* "
        const sp = body.indexOf(' ');
        if (sp < 0) return;
        const tag = body.slice(0, sp), rest = body.slice(sp + 1);
        const msg = this.pending[tag];
        if (!msg) return;
        const colon = rest.indexOf(': ');
        if (colon < 0) return;
        const key = rest.slice(0, colon), value = rest.slice(colon + 2);
        const prev = msg.attrs[key] || '';
        const next = prev ? prev + '\n' + value : value;
        // Approximate delta: new chars minus old (plus one newline when joining).
        const delta = next.length - prev.length;
        if (msg._byteSize + delta > MCP_MAX_MESSAGE_BYTES) {
            this._dropPending(tag, 'per-message size limit');
            return;
        }
        if (this._pendingBytes + delta > MCP_MAX_TOTAL_PENDING_BYTES) {
            this._dropPending(tag, 'total pending size limit');
            return;
        }
        msg.attrs[key] = next;
        msg._byteSize += delta;
        this._pendingBytes += delta;
    }

    _handleEnd(line) {
        const tag = line.slice(5).trim(); // remove "#$#: "
        const msg = this.pending[tag];
        if (!msg) return;
        this._pendingBytes -= (msg._byteSize || 0);
        if (this._pendingBytes < 0) this._pendingBytes = 0;
        delete this.pending[tag];
        this._dispatch(msg);
    }

    _dispatch(msg) {
        switch (msg.messageName) {
        case 'mcp': this._handleInit(msg); break;
        case 'mcp-negotiate-can': this._handleNegCan(msg); break;
        case 'mcp-negotiate-end': this.negotiated = true; break;
        case 'dns-org-mud-moo-simpleedit-content': this._handleEditContent(msg); break;
        }
    }

    _handleInit(msg) {
        const sMin = parseFloat(msg.attrs.version), sMax = parseFloat(msg.attrs.to);
        if (isNaN(sMin) || isNaN(sMax)) return;
        if (2.1 < sMin || sMax < 2.1) return;
        this.sessionKey = this._genKey();
        this.sendRaw(`#$#mcp authentication-key: ${this.sessionKey} version: 2.1 to: 2.1`);
        this.sendRaw(`#$#mcp-negotiate-can ${this.sessionKey} package: dns-org-mud-moo-simpleedit min-version: 1.0 max-version: 1.0`);
        this.sendRaw(`#$#mcp-negotiate-end ${this.sessionKey}`);
    }

    _handleNegCan(msg) {
        const pkg = msg.attrs.package;
        if (pkg) this.serverPackages[pkg] = { min: msg.attrs['min-version'], max: msg.attrs['max-version'] };
    }

    _handleEditContent(msg) {
        const ref = msg.attrs.reference || '';
        const name = msg.attrs.name || ref;
        const type = msg.attrs.type || 'string-list';
        const content = msg.attrs.content || '';
        if (this.onEditRequest) this.onEditRequest(ref, name, type, content);
    }

    sendEditSet(reference, type, content) {
        if (!this.sessionKey) return;
        const tag = 'T' + (++this.tagCounter);
        this.sendRaw(`#$#dns-org-mud-moo-simpleedit-set ${this.sessionKey} reference: ${reference} type: ${type} content*: "" _data-tag: ${tag}`);
        for (const line of content.split('\n')) {
            this.sendRaw(`#$#* ${tag} content: ${line}`);
        }
        this.sendRaw(`#$#: ${tag}`);
    }

    _genKey() {
        const c = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
        return Array.from({length: 16}, () => c[Math.random() * c.length | 0]).join('');
    }
}
