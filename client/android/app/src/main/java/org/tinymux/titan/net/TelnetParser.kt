package org.tinymux.titan.net

class TelnetParser(private val sendRaw: (ByteArray) -> Unit) {
    companion object {
        const val IAC: Int = 255; const val DONT: Int = 254; const val DO: Int = 253
        const val WONT: Int = 252; const val WILL: Int = 251; const val SB: Int = 250
        const val GA: Int = 249; const val SE: Int = 240

        const val ECHO: Int = 1; const val SGA: Int = 3; const val TTYPE: Int = 24
        const val NAWS: Int = 31; const val CHARSET: Int = 42
        const val MSSP: Int = 70; const val GMCP: Int = 201
    }

    private enum class State { DATA, IAC, WILL, WONT, DO, DONT, SB, SB_DATA, SB_IAC }

    private var state = State.DATA
    private var sbOption = 0
    private val sbBuf = mutableListOf<Byte>()
    private val lineBuf = StringBuilder()

    var remoteEcho = false; private set
    var nawsAgreed = false; private set
    var nawsWidth = 80; var nawsHeight = 24
    val gmcp = mutableMapOf<String, String>()
    val mssp = mutableMapOf<String, String>()

    var onLine: ((String) -> Unit)? = null
    var onPrompt: ((String) -> Unit)? = null

    fun process(data: ByteArray, offset: Int = 0, length: Int = data.size) {
        for (i in offset until offset + length) {
            val c = data[i].toInt() and 0xFF
            when (state) {
                State.DATA -> if (c == IAC) state = State.IAC else onData(c)
                State.IAC -> when (c) {
                    IAC -> { onData(c); state = State.DATA }
                    WILL -> state = State.WILL; WONT -> state = State.WONT
                    DO -> state = State.DO; DONT -> state = State.DONT
                    SB -> state = State.SB
                    GA -> {
                        if (lineBuf.isNotEmpty()) { onPrompt?.invoke(lineBuf.toString()); lineBuf.clear() }
                        state = State.DATA
                    }
                    else -> state = State.DATA
                }
                State.WILL -> {
                    when (c) {
                        ECHO -> { remoteEcho = true; send(DO, c) }
                        SGA -> send(DO, c)
                        GMCP -> send(DO, c)
                        else -> send(DONT, c)
                    }; state = State.DATA
                }
                State.WONT -> { if (c == ECHO) remoteEcho = false; state = State.DATA }
                State.DO -> {
                    when (c) {
                        NAWS -> { nawsAgreed = true; sendNaws() }
                        TTYPE -> sendTtype()
                        else -> send(WONT, c)
                    }; state = State.DATA
                }
                State.DONT -> { if (c == NAWS) nawsAgreed = false; state = State.DATA }
                State.SB -> { sbOption = c; sbBuf.clear(); state = State.SB_DATA }
                State.SB_DATA -> if (c == IAC) state = State.SB_IAC else sbBuf.add(c.toByte())
                State.SB_IAC -> when (c) {
                    SE -> { handleSubneg(); state = State.DATA }
                    IAC -> { sbBuf.add(0xFF.toByte()); state = State.SB_DATA }
                    else -> state = State.DATA
                }
            }
        }
    }

    private fun onData(c: Int) {
        if (c == 10) {
            val line = lineBuf.toString().trimEnd('\r')
            onLine?.invoke(line)
            lineBuf.clear()
        } else {
            lineBuf.append(c.toChar())
        }
    }

    private fun send(cmd: Int, opt: Int) = sendRaw(byteArrayOf(IAC.toByte(), cmd.toByte(), opt.toByte()))

    fun sendNaws() {
        if (!nawsAgreed) return
        sendRaw(byteArrayOf(
            IAC.toByte(), SB.toByte(), NAWS.toByte(),
            (nawsWidth shr 8).toByte(), (nawsWidth and 0xFF).toByte(),
            (nawsHeight shr 8).toByte(), (nawsHeight and 0xFF).toByte(),
            IAC.toByte(), SE.toByte()
        ))
    }

    private fun sendTtype() {
        val ttype = "Titan-Android"
        val buf = mutableListOf(IAC.toByte(), SB.toByte(), TTYPE.toByte(), 0.toByte())
        ttype.forEach { buf.add(it.code.toByte()) }
        buf.addAll(listOf(IAC.toByte(), SE.toByte()))
        sendRaw(buf.toByteArray())
    }

    fun sendLine(text: String) = sendRaw("$text\r\n".toByteArray(Charsets.UTF_8))

    private fun handleSubneg() {
        val data = sbBuf.toByteArray()
        when (sbOption) {
            TTYPE -> if (data.isNotEmpty() && data[0].toInt() == 1) sendTtype()
            CHARSET -> handleCharset(data)
            GMCP -> {
                val text = String(data, Charsets.UTF_8)
                val sp = text.indexOf(' ')
                if (sp >= 0) gmcp[text.substring(0, sp)] = text.substring(sp + 1)
                else gmcp[text] = ""
            }
            MSSP -> handleMssp(data)
        }
    }

    private fun handleCharset(data: ByteArray) {
        if (data.isEmpty() || data[0].toInt() != 1) return
        val offered = String(data, 1, data.size - 1, Charsets.UTF_8)
        val delim = if (offered.isNotEmpty()) offered[0] else return
        val charsets = offered.substring(1).split(delim)
        val hasUtf8 = charsets.any { it.equals("UTF-8", ignoreCase = true) }
        val resp = mutableListOf(IAC.toByte(), SB.toByte(), CHARSET.toByte())
        if (hasUtf8) { resp.add(2); "UTF-8".forEach { resp.add(it.code.toByte()) } }
        else resp.add(3)
        resp.addAll(listOf(IAC.toByte(), SE.toByte()))
        sendRaw(resp.toByteArray())
    }

    private fun handleMssp(data: ByteArray) {
        var key = ""; var value = ""; var mode = 0
        for (b in data) {
            val v = b.toInt() and 0xFF
            when {
                v == 1 -> { if (mode == 2 && key.isNotEmpty()) mssp[key] = value; key = ""; value = ""; mode = 1 }
                v == 2 -> mode = 2
                mode == 1 -> key += v.toChar()
                mode == 2 -> value += v.toChar()
            }
        }
        if (mode == 2 && key.isNotEmpty()) mssp[key] = value
    }
}
