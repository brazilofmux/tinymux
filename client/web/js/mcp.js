// mcp.js -- MCP 2.1 protocol parser for the web client.
'use strict';

class McpParser {
    constructor() {
        this.sessionKey = null;
        this.negotiated = false;
        this.pending = {};          // dataTag -> McpMessage
        this.serverPackages = {};
        this.tagCounter = 0;
        this.onEditRequest = null;  // callback(reference, name, type, content)
        this.sendRaw = null;        // callback(string)
    }

    // Process a line. Returns true if MCP (should be hidden from display).
    processLine(line) {
        if (!line.startsWith('#$#')) return false;
        if (line.startsWith('#$#*')) this._handleContinuation(line);
        else if (line.startsWith('#$#:')) this._handleEnd(line);
        else this._handleMessage(line);
        return true;
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

        const msg = { messageName, authKey, attrs: {}, multilineKeys: new Set(), dataTag: null };
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
            this.pending[msg.dataTag] = msg;
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
        msg.attrs[key] = msg.attrs[key] ? msg.attrs[key] + '\n' + value : value;
    }

    _handleEnd(line) {
        const tag = line.slice(5).trim(); // remove "#$#: "
        const msg = this.pending[tag];
        if (!msg) return;
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
