package org.tinymux.titan.data

import android.content.Context
import java.io.File
import java.io.FileWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class SessionLogger(private val context: Context) {
    private var writer: FileWriter? = null
    private var logFile: File? = null
    var active = false; private set

    val logDir: File get() = File(context.filesDir, "logs").also { it.mkdirs() }

    fun start(worldName: String, filename: String? = null): File {
        stop()
        val safeName = worldName.replace(Regex("[^a-zA-Z0-9._-]"), "_")
        val ts = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val name = filename ?: "${safeName}_$ts.log"
        val file = File(logDir, name)
        writer = FileWriter(file, true)
        logFile = file
        active = true
        writeLine("--- Log started: ${Date()} ---")
        return file
    }

    fun stop() {
        if (!active) return
        writeLine("--- Log ended: ${Date()} ---")
        try { writer?.close() } catch (_: Exception) {}
        writer = null
        logFile = null
        active = false
    }

    fun writeLine(line: String) {
        if (!active) return
        try {
            writer?.write(line)
            writer?.write("\n")
            writer?.flush()
        } catch (_: Exception) {}
    }

    fun currentFile(): File? = logFile
}
