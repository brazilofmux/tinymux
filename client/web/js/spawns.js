// spawns.js -- Spawn (output routing) system for the web client.
'use strict';

class SpawnDB {
    constructor() {
        this.spawns = [];  // [{name, path, patterns, exceptions, prefix, maxLines, weight}]
    }

    add(def) {
        const s = {
            name: def.name || '',
            path: def.path || def.name.toLowerCase(),
            patterns: def.patterns || [],
            exceptions: def.exceptions || [],
            prefix: def.prefix || '',
            maxLines: def.maxLines || 20000,
            weight: def.weight || 0,
        };
        s._compiled = s.patterns.map(p => { try { return new RegExp(p, 'i'); } catch(e) { return null; } }).filter(Boolean);
        s._exceptions = s.exceptions.map(p => { try { return new RegExp(p, 'i'); } catch(e) { return null; } }).filter(Boolean);

        const idx = this.spawns.findIndex(x => x.path === s.path);
        if (idx >= 0) this.spawns[idx] = s;
        else this.spawns.push(s);
        this.spawns.sort((a, b) => a.weight - b.weight);
    }

    remove(path) {
        this.spawns = this.spawns.filter(s => s.path !== path);
    }

    list() { return this.spawns; }

    // Returns array of spawn paths that match this line
    match(line) {
        const matched = [];
        for (const s of this.spawns) {
            if (!s._compiled.length) continue;
            const hits = s._compiled.some(r => r.test(line));
            if (!hits) continue;
            const excepted = s._exceptions.some(r => r.test(line));
            if (excepted) continue;
            matched.push(s.path);
        }
        return matched;
    }

    loadFrom(defs) {
        this.spawns = [];
        for (const d of defs) this.add(d);
    }

    toJSON() {
        return this.spawns.map(s => ({
            name: s.name, path: s.path, patterns: s.patterns,
            exceptions: s.exceptions, prefix: s.prefix,
            maxLines: s.maxLines, weight: s.weight,
        }));
    }
}
