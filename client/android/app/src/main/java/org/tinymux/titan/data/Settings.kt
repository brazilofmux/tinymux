package org.tinymux.titan.data

import android.content.Context

class AppSettings(context: Context) {
    private val prefs = context.getSharedPreferences("titan_settings", Context.MODE_PRIVATE)

    var fontSize: Int
        get() = prefs.getInt("font_size", 14)
        set(v) = prefs.edit().putInt("font_size", v).apply()

    var fontSizeLandscape: Int
        get() = prefs.getInt("font_size_landscape", 12)
        set(v) = prefs.edit().putInt("font_size_landscape", v).apply()

    var scrollbackLines: Int
        get() = prefs.getInt("scrollback_lines", 20000)
        set(v) = prefs.edit().putInt("scrollback_lines", v).apply()

    var defaultPort: Int
        get() = prefs.getInt("default_port", 4201)
        set(v) = prefs.edit().putInt("default_port", v).apply()

    var defaultSsl: Boolean
        get() = prefs.getBoolean("default_ssl", false)
        set(v) = prefs.edit().putBoolean("default_ssl", v).apply()

    var theme: String
        get() = prefs.getString("theme", "dark") ?: "dark"
        set(v) = prefs.edit().putString("theme", v).apply()

    var keepScreenOn: Boolean
        get() = prefs.getBoolean("keep_screen_on", false)
        set(v) = prefs.edit().putBoolean("keep_screen_on", v).apply()
}
