// triggers.js -- Trigger system for the web client.
'use strict';

class TriggerDB {
    constructor() {
        this.triggers = [];
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
            hilite: !!def.hilite,
            substituteFind: def.substituteFind || '',
            substituteReplace: def.substituteReplace || '',
            lineClass: def.lineClass || '',
            speak: !!def.speak,
            enabled: def.enabled !== false,
            regex: null,
        };
        try {
            t.regex = new RegExp(t.pattern, 'i');
        } catch (e) {
            t.regex = null;
        }
        const idx = this.triggers.findIndex(x => x.name === t.name);
        if (idx >= 0) this.triggers[idx] = t;
        else this.triggers.push(t);
        this.triggers.sort((a, b) => b.priority - a.priority);
    }

    remove(name) {
        this.triggers = this.triggers.filter(t => t.name !== name);
    }

    // Check a line against all triggers.
    check(line, lineClasses = new Set()) {
        const result = {
            matched: false, gagged: false, commands: [],
            displayLine: null, lineClasses: new Set(), speakText: null,
        };
        for (const t of this.triggers) {
            if (!t.enabled || !t.regex || t.shots === 0) continue;
            const m = t.regex.exec(line);
            if (!m) continue;

            result.matched = true;
            if (t.gag) result.gagged = true;

            // Highlight
            if (t.hilite && !result.gagged) {
                const target = result.displayLine || line;
                const m2 = t.regex.exec(target);
                if (m2) {
                    result.displayLine = target.slice(0, m2.index) +
                        '\x1b[1m' + m2[0] + '\x1b[22m' +
                        target.slice(m2.index + m2[0].length);
                }
            }

            // Substitution
            if (t.substituteFind && !result.gagged) {
                try {
                    const subRe = new RegExp(t.substituteFind, 'gi');
                    const target = result.displayLine || line;
                    result.displayLine = target.replace(subRe, t.substituteReplace);
                } catch (e) {}
            }

            // TTS
            if (t.speak && !result.gagged) {
                result.speakText = line;
            }

            // Line classification
            if (t.lineClass) {
                result.lineClasses.add(t.lineClass);
                lineClasses.add(t.lineClass);
            }

            // Command
            if (t.body) {
                let cmd = t.body;
                if (m[0] !== undefined) cmd = cmd.replace(/\$0/g, m[0]);
                if (m[1] !== undefined) cmd = cmd.replace(/\$1/g, m[1]);
                if (m[2] !== undefined) cmd = cmd.replace(/\$2/g, m[2]);
                result.commands.push(cmd);
            }

            if (t.shots > 0) t.shots--;
        }
        return result;
    }

    list() { return this.triggers; }

    loadFrom(defs) {
        this.triggers = [];
        for (const d of defs) this.add(d);
    }

    toJSON() {
        return this.triggers.map(t => ({
            name: t.name, pattern: t.pattern, body: t.body,
            priority: t.priority, shots: t.shots, gag: t.gag,
            hilite: t.hilite, substituteFind: t.substituteFind,
            substituteReplace: t.substituteReplace, lineClass: t.lineClass,
            speak: t.speak, enabled: t.enabled,
        }));
    }
}
