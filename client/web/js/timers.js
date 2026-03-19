// timers.js -- Timer system for the web client.
'use strict';

class TimerDB {
    constructor() {
        this.timers = {};  // name -> {name, command, intervalMs, shots, handle}
    }

    add(name, command, intervalMs, shots = -1) {
        this.remove(name);
        const timer = { name, command, intervalMs, shotsRemaining: shots, handle: null };
        timer.handle = setInterval(() => {
            if (this.onFire) this.onFire(name, command);
            if (timer.shotsRemaining > 0) {
                timer.shotsRemaining--;
                if (timer.shotsRemaining === 0) {
                    this.remove(name);
                }
            }
        }, intervalMs);
        this.timers[name] = timer;
    }

    remove(name) {
        const t = this.timers[name];
        if (t) { clearInterval(t.handle); delete this.timers[name]; return true; }
        return false;
    }

    list() { return Object.values(this.timers); }

    cancelAll() {
        for (const name of Object.keys(this.timers)) this.remove(name);
    }

    onFire = null;  // callback(name, command)
}
