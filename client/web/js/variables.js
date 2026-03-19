// variables.js -- Variable namespace for the web client.
'use strict';

class VariableStore {
    constructor() {
        this.temp = {};       // temp.* — session variables
        this.worldTemp = {};  // worldtemp.* — per-world session
        this.regexpCaptures = [];  // regexp.0, regexp.1, ...
    }

    resolve(key, ctx = {}) {
        const parts = key.split('.', 2);
        if (parts.length < 2) return this.temp[key];

        const [ns, name] = parts;
        switch (ns) {
        case 'world':
            return { name: ctx.worldName, character: ctx.character,
                     host: ctx.host, port: ctx.port,
                     connected: ctx.connected ? '1' : '0' }[name];
        case 'event':
            return { line: ctx.eventLine, cause: ctx.eventCause }[name];
        case 'regexp':
            return this.regexpCaptures[parseInt(name)];
        case 'datetime':
            return this._datetime(name);
        case 'temp':
            return this.temp[name];
        case 'worldtemp':
            return this.worldTemp[name];
        }
        return undefined;
    }

    _datetime(name) {
        const d = new Date();
        const pad = (n) => String(n).padStart(2, '0');
        const days = ['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
        const months = ['January','February','March','April','May','June',
                        'July','August','September','October','November','December'];
        switch (name) {
        case 'date': return `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())}`;
        case 'time': return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
        case 'year': return String(d.getFullYear());
        case 'month': return pad(d.getMonth()+1);
        case 'day': return pad(d.getDate());
        case 'hour': return pad(d.getHours());
        case 'minute': return pad(d.getMinutes());
        case 'second': return pad(d.getSeconds());
        case 'weekday': return days[d.getDay()];
        case 'weekdayshort': return days[d.getDay()].slice(0,3);
        case 'monthname': return months[d.getMonth()];
        case 'monthnameshort': return months[d.getMonth()].slice(0,3);
        }
        return undefined;
    }

    expand(text, ctx = {}) {
        return text.replace(/\$([a-zA-Z_][a-zA-Z0-9_.]+)/g, (match, key) => {
            return this.resolve(key, ctx) ?? match;
        });
    }
}
