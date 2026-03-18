// settings.js -- localStorage-based settings for the web client.
'use strict';

const Settings = {
    _key: 'titan_settings',

    _defaults: {
        worlds: [],
        triggers: [],
        font_name: 'Consolas, Courier New, monospace',
        font_size: 14,
        scrollback_lines: 20000,
    },

    _data: null,

    load() {
        try {
            const raw = localStorage.getItem(this._key);
            this._data = raw ? { ...this._defaults, ...JSON.parse(raw) } : { ...this._defaults };
        } catch (e) {
            this._data = { ...this._defaults };
        }
    },

    save() {
        try {
            localStorage.setItem(this._key, JSON.stringify(this._data));
        } catch (e) {}
    },

    get(key) {
        if (!this._data) this.load();
        return this._data[key] !== undefined ? this._data[key] : this._defaults[key];
    },

    set(key, value) {
        if (!this._data) this.load();
        this._data[key] = value;
        this.save();
    },

    // World helpers
    getWorlds() { return this.get('worlds') || []; },

    saveWorlds(worlds) { this.set('worlds', worlds); },

    addWorld(world) {
        const worlds = this.getWorlds();
        const idx = worlds.findIndex(w => w.name === world.name);
        if (idx >= 0) worlds[idx] = world;
        else worlds.push(world);
        this.saveWorlds(worlds);
    },

    removeWorld(name) {
        const worlds = this.getWorlds().filter(w => w.name !== name);
        this.saveWorlds(worlds);
    },
};
