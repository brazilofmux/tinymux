// triggers.js -- Trigger system for the web client.
'use strict';

class TriggerDB {
    constructor() {
        this.triggers = [];  // [{name, pattern, body, regex, priority, shots, gag}]
        this.nextId = 1;
    }

    add(def) {
        const t = {
            id: this.nextId++,
            name: def.name || ('_trig_' + this.nextId),
            pattern: def.pattern || '',
            body: def.body || '',
            priority: def.priority || 0,
            shots: def.shots !== undefined ? def.shots : -1,
            gag: !!def.gag,
            regex: null,
        };
        try {
            t.regex = new RegExp(t.pattern, 'i');
        } catch (e) {
            t.regex = null;
        }
        // Replace existing with same name
        const idx = this.triggers.findIndex(x => x.name === t.name);
        if (idx >= 0) this.triggers[idx] = t;
        else this.triggers.push(t);
        this.triggers.sort((a, b) => b.priority - a.priority);
    }

    remove(name) {
        this.triggers = this.triggers.filter(t => t.name !== name);
    }

    // Check a line against all triggers. Returns {matched, gagged, commands[]}.
    check(line) {
        const result = { matched: false, gagged: false, commands: [] };
        for (const t of this.triggers) {
            if (!t.regex || t.shots === 0) continue;
            if (t.regex.test(line)) {
                result.matched = true;
                if (t.gag) result.gagged = true;
                if (t.body) result.commands.push(t.body);
                if (t.shots > 0) t.shots--;
            }
        }
        return result;
    }

    list() { return this.triggers; }

    // Load from settings
    loadFrom(defs) {
        this.triggers = [];
        for (const d of defs) this.add(d);
    }

    // Export for settings
    toJSON() {
        return this.triggers.map(t => ({
            name: t.name,
            pattern: t.pattern,
            body: t.body,
            priority: t.priority,
            shots: t.shots,
            gag: t.gag,
        }));
    }
}
