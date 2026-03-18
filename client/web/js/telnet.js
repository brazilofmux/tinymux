// telnet.js -- Telnet protocol parser for the web client.
'use strict';

const TEL = {
    IAC: 255, DONT: 254, DO: 253, WONT: 252, WILL: 251,
    SB: 250, GA: 249, SE: 240,
    // Options
    ECHO: 1, SGA: 3, TTYPE: 24, NAWS: 31, CHARSET: 42,
    MSSP: 70, GMCP: 201,
    // Subneg
    TTYPE_IS: 0, TTYPE_SEND: 1,
    CHARSET_REQUEST: 1, CHARSET_ACCEPTED: 2, CHARSET_REJECTED: 3,
};

class TelnetParser {
    constructor(sendRaw) {
        this.sendRaw = sendRaw;  // function(Uint8Array)
        this.state = 'DATA';
        this.sbOption = 0;
        this.sbBuf = [];
        this.remoteEcho = false;
        this.nawsAgreed = false;
        this.nawsWidth = 80;
        this.nawsHeight = 24;
        this.gmcp = {};
        this.mssp = {};
        this.onLine = null;     // callback(string)
        this.onPrompt = null;   // callback(string)
        this.lineBuf = '';
    }

    process(data) {
        const bytes = new Uint8Array(data);
        for (let i = 0; i < bytes.length; i++) {
            const c = bytes[i];
            switch (this.state) {
            case 'DATA':
                if (c === TEL.IAC) { this.state = 'IAC'; }
                else { this._onData(c); }
                break;
            case 'IAC':
                switch (c) {
                case TEL.IAC:  this._onData(c); this.state = 'DATA'; break;
                case TEL.WILL: this.state = 'WILL'; break;
                case TEL.WONT: this.state = 'WONT'; break;
                case TEL.DO:   this.state = 'DO'; break;
                case TEL.DONT: this.state = 'DONT'; break;
                case TEL.SB:   this.state = 'SB'; break;
                case TEL.GA:
                    if (this.lineBuf && this.onPrompt) {
                        this.onPrompt(this.lineBuf);
                        this.lineBuf = '';
                    }
                    this.state = 'DATA';
                    break;
                default: this.state = 'DATA'; break;
                }
                break;
            case 'WILL':
                if (c === TEL.ECHO) { this.remoteEcho = true; this._send(TEL.DO, c); }
                else if (c === TEL.SGA) { this._send(TEL.DO, c); }
                else if (c === TEL.GMCP) { this._send(TEL.DO, c); }
                else { this._send(TEL.DONT, c); }
                this.state = 'DATA';
                break;
            case 'WONT':
                if (c === TEL.ECHO) this.remoteEcho = false;
                this.state = 'DATA';
                break;
            case 'DO':
                if (c === TEL.NAWS) { this.nawsAgreed = true; this.sendNaws(); }
                else if (c === TEL.TTYPE) { this._sendTtype(); }
                else if (c === TEL.CHARSET) { /* wait for SB */ }
                else { this._send(TEL.WONT, c); }
                this.state = 'DATA';
                break;
            case 'DONT':
                if (c === TEL.NAWS) this.nawsAgreed = false;
                this.state = 'DATA';
                break;
            case 'SB':
                this.sbOption = c;
                this.sbBuf = [];
                this.state = 'SB_DATA';
                break;
            case 'SB_DATA':
                if (c === TEL.IAC) this.state = 'SB_IAC';
                else this.sbBuf.push(c);
                break;
            case 'SB_IAC':
                if (c === TEL.SE) {
                    this._handleSubneg();
                    this.state = 'DATA';
                } else if (c === TEL.IAC) {
                    this.sbBuf.push(0xFF);
                    this.state = 'SB_DATA';
                } else {
                    this.state = 'DATA';
                }
                break;
            }
        }
    }

    _onData(c) {
        if (c === 10) { // \n
            let line = this.lineBuf;
            if (line.endsWith('\r')) line = line.slice(0, -1);
            if (this.onLine) this.onLine(line);
            this.lineBuf = '';
        } else {
            this.lineBuf += String.fromCharCode(c);
        }
    }

    _send(cmd, opt) {
        this.sendRaw(new Uint8Array([TEL.IAC, cmd, opt]));
    }

    _sendTtype() {
        const ttype = 'Titan-Web';
        const buf = [TEL.IAC, TEL.SB, TEL.TTYPE, TEL.TTYPE_IS];
        for (let i = 0; i < ttype.length; i++) buf.push(ttype.charCodeAt(i));
        buf.push(TEL.IAC, TEL.SE);
        this.sendRaw(new Uint8Array(buf));
    }

    sendNaws() {
        if (!this.nawsAgreed) return;
        const w = this.nawsWidth, h = this.nawsHeight;
        this.sendRaw(new Uint8Array([
            TEL.IAC, TEL.SB, TEL.NAWS,
            (w >> 8) & 0xFF, w & 0xFF,
            (h >> 8) & 0xFF, h & 0xFF,
            TEL.IAC, TEL.SE
        ]));
    }

    sendLine(text) {
        const encoder = new TextEncoder();
        const bytes = encoder.encode(text + '\r\n');
        this.sendRaw(bytes);
    }

    _handleSubneg() {
        if (this.sbOption === TEL.TTYPE && this.sbBuf.length > 0 &&
            this.sbBuf[0] === TEL.TTYPE_SEND) {
            this._sendTtype();
        } else if (this.sbOption === TEL.CHARSET && this.sbBuf.length > 0 &&
                   this.sbBuf[0] === TEL.CHARSET_REQUEST) {
            // Look for UTF-8
            const offered = String.fromCharCode(...this.sbBuf.slice(1));
            const delim = offered.charAt(0);
            const charsets = offered.slice(1).split(delim);
            const hasUtf8 = charsets.some(c => c.toUpperCase() === 'UTF-8');
            const resp = [TEL.IAC, TEL.SB, TEL.CHARSET];
            if (hasUtf8) {
                resp.push(TEL.CHARSET_ACCEPTED);
                for (const ch of 'UTF-8') resp.push(ch.charCodeAt(0));
            } else {
                resp.push(TEL.CHARSET_REJECTED);
            }
            resp.push(TEL.IAC, TEL.SE);
            this.sendRaw(new Uint8Array(resp));
        } else if (this.sbOption === TEL.GMCP) {
            const text = String.fromCharCode(...this.sbBuf);
            const sp = text.indexOf(' ');
            if (sp >= 0) {
                this.gmcp[text.slice(0, sp)] = text.slice(sp + 1);
            } else {
                this.gmcp[text] = '';
            }
        } else if (this.sbOption === TEL.MSSP) {
            let key = '', val = '', state = 0;
            for (const b of this.sbBuf) {
                if (b === 1) { // MSSP_VAR
                    if (state === 2 && key) this.mssp[key] = val;
                    key = ''; val = ''; state = 1;
                } else if (b === 2) { // MSSP_VAL
                    state = 2;
                } else if (state === 1) {
                    key += String.fromCharCode(b);
                } else if (state === 2) {
                    val += String.fromCharCode(b);
                }
            }
            if (state === 2 && key) this.mssp[key] = val;
        }
    }
}
