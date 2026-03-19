// hooks.js -- Hook system for the web client.
'use strict';

class HookDB {
    constructor() {
        this.hooks = [];  // [{name, event, body, enabled}]
    }

    static EVENTS = ['CONNECT', 'DISCONNECT', 'ACTIVITY'];

    add(def) {
        const h = {
            name: def.name || '',
            event: (def.event || '').toUpperCase(),
            body: def.body || '',
            enabled: def.enabled !== false,
        };
        const idx = this.hooks.findIndex(x => x.name === h.name);
        if (idx >= 0) this.hooks[idx] = h;
        else this.hooks.push(h);
    }

    remove(name) {
        this.hooks = this.hooks.filter(h => h.name !== name);
    }

    list() { return this.hooks; }

    fireEvent(event) {
        return this.hooks
            .filter(h => h.enabled && h.event === event.toUpperCase())
            .map(h => h.body);
    }

    loadFrom(defs) {
        this.hooks = [];
        for (const d of defs) this.add(d);
    }

    toJSON() {
        return this.hooks.map(h => ({
            name: h.name, event: h.event, body: h.body, enabled: h.enabled,
        }));
    }
}
