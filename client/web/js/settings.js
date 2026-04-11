// settings.js -- localStorage-based settings for the web client.
'use strict';

const Settings = {
    _key: 'titan_settings',
    _passwordKeyPrefix: 'titan_world_password:',

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
            let migrated = false;
            if (Array.isArray(this._data.worlds)) {
                this._data.worlds = this._data.worlds.map(w => {
                    if (w && w.password) {
                        this.setWorldPassword(w.name, w.password);
                        const copy = { ...w };
                        delete copy.password;
                        migrated = true;
                        return copy;
                    }
                    return w;
                });
            }
            if (migrated) this.save();
        } catch (e) {
            this._data = { ...this._defaults };
        }
    },

    save() {
        try {
            localStorage.setItem(this._key, JSON.stringify(this._data));
        } catch (e) {}
    },

    _worldPasswordKey(name) {
        return this._passwordKeyPrefix + name;
    },

    _splitWorldPassword(world) {
        const copy = { ...world };
        const password = copy.password || '';
        delete copy.password;
        return { world: copy, password };
    },

    _mergeWorldPassword(world) {
        return {
            ...world,
            password: this.getWorldPassword(world.name),
        };
    },

    getWorldPassword(name) {
        try {
            return sessionStorage.getItem(this._worldPasswordKey(name)) || '';
        } catch (e) {
            return '';
        }
    },

    setWorldPassword(name, password) {
        try {
            const key = this._worldPasswordKey(name);
            if (password) sessionStorage.setItem(key, password);
            else sessionStorage.removeItem(key);
        } catch (e) {}
    },

    get(key) {
        if (!this._data) this.load();
        if (key === 'worlds') {
            const worlds = this._data.worlds !== undefined ? this._data.worlds : this._defaults.worlds;
            return worlds.map(w => this._mergeWorldPassword(w));
        }
        return this._data[key] !== undefined ? this._data[key] : this._defaults[key];
    },

    set(key, value) {
        if (!this._data) this.load();
        this._data[key] = value;
        this.save();
    },

    // World helpers
    getWorlds() { return this.get('worlds') || []; },

    saveWorlds(worlds) {
        if (!this._data) this.load();
        this._data.worlds = worlds.map(w => {
            const { world, password } = this._splitWorldPassword(w);
            this.setWorldPassword(world.name, password);
            return world;
        });
        this.save();
    },

    addWorld(world) {
        const worlds = this.getWorlds();
        const idx = worlds.findIndex(w => w.name === world.name);
        if (idx >= 0) worlds[idx] = world;
        else worlds.push(world);
        this.saveWorlds(worlds);
    },

    removeWorld(name) {
        const worlds = this.getWorlds().filter(w => w.name !== name);
        this.setWorldPassword(name, '');
        this.saveWorlds(worlds);
    },
};
