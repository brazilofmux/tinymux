package org.tinymux.titan.data

import kotlinx.coroutines.*

data class MudTimer(
    val name: String,
    val command: String,
    val intervalMs: Long,
    var shotsRemaining: Int = -1,
)

class TimerEngine(private val scope: CoroutineScope) {
    private val timers = mutableMapOf<String, Pair<MudTimer, Job>>()
    var onFire: ((timerName: String, command: String) -> Unit)? = null

    fun add(name: String, command: String, intervalMs: Long, shots: Int = -1) {
        remove(name)
        val timer = MudTimer(name, command, intervalMs, shots)
        val job = scope.launch {
            while (isActive) {
                delay(intervalMs)
                onFire?.invoke(name, command)
                if (timer.shotsRemaining > 0) {
                    timer.shotsRemaining--
                    if (timer.shotsRemaining == 0) {
                        timers.remove(name)
                        break
                    }
                }
            }
        }
        timers[name] = timer to job
    }

    fun remove(name: String): Boolean {
        val entry = timers.remove(name)
        entry?.second?.cancel()
        return entry != null
    }

    fun list(): List<MudTimer> = timers.values.map { it.first }

    fun cancelAll() {
        timers.values.forEach { it.second.cancel() }
        timers.clear()
    }
}
