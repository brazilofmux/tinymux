// connection.js -- WebSocket connection with telnet protocol.
'use strict';

class Connection {
    constructor(name, host, port, ssl) {
        this.name = name;
        this.host = host;
        this.port = port;
        this.ssl = ssl;
        this.ws = null;
        this.telnet = null;
        this.connected = false;
        this.onLine = null;      // callback(line)
        this.onPrompt = null;    // callback(prompt)
        this.onConnect = null;   // callback()
        this.onDisconnect = null;// callback()
        this.scrollback = [];
        this.maxScrollback = 20000;
    }

    connect() {
        const proto = this.ssl ? 'wss' : 'ws';
        const url = `${proto}://${this.host}:${this.port}/`;

        try {
            this.ws = new WebSocket(url);
            this.ws.binaryType = 'arraybuffer';
        } catch (e) {
            return false;
        }

        this.telnet = new TelnetParser((data) => {
            if (this.ws && this.ws.readyState === WebSocket.OPEN) {
                this.ws.send(data);
            }
        });

        this.telnet.onLine = (line) => {
            this.addScrollback(line);
            if (this.onLine) this.onLine(line);
        };

        this.telnet.onPrompt = (prompt) => {
            this.addScrollback(prompt);
            if (this.onPrompt) this.onPrompt(prompt);
        };

        this.ws.onopen = () => {
            this.connected = true;
            // Send initial telnet negotiations
            const raw = this.telnet.sendRaw.bind(this.telnet);
            this.telnet.sendRaw(new Uint8Array([255, 251, 31]));  // WILL NAWS
            this.telnet.sendRaw(new Uint8Array([255, 251, 24]));  // WILL TTYPE
            this.telnet.sendRaw(new Uint8Array([255, 251, 42]));  // WILL CHARSET
            this.telnet.sendRaw(new Uint8Array([255, 253, 3]));   // DO SGA
            this.telnet.sendRaw(new Uint8Array([255, 253, 1]));   // DO ECHO
            if (this.onConnect) this.onConnect();
        };

        this.ws.onmessage = (event) => {
            this.telnet.process(event.data);
        };

        this.ws.onclose = () => {
            this.connected = false;
            if (this.onDisconnect) this.onDisconnect();
        };

        this.ws.onerror = () => {
            this.connected = false;
            if (this.onDisconnect) this.onDisconnect();
        };

        return true;
    }

    disconnect() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
        this.connected = false;
    }

    sendLine(text) {
        if (!this.connected || !this.telnet) return;
        this.telnet.sendLine(text);
    }

    sendNaws(width, height) {
        if (!this.telnet) return;
        this.telnet.nawsWidth = width;
        this.telnet.nawsHeight = height;
        this.telnet.sendNaws();
    }

    addScrollback(line) {
        this.scrollback.push(line);
        while (this.scrollback.length > this.maxScrollback) {
            this.scrollback.shift();
        }
    }
}
