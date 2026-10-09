package com.cardiomonitor.app.data.importer

import java.io.BufferedReader
import kotlin.math.roundToInt

/** Parses firmware CSV by column name, including the different Pulse/SpO2 column orders.
 * Times are elapsed session milliseconds, NOT wall-clock dates. Invalid values remain absent.
 */
class SdCsvReader(private val reader: BufferedReader) {
    data class Row(val ms: Long, val ecg: Float?, val bpm: Int?, val spo2: Int?)
    private var columns: Map<String, Int>? = null
    var startedAtMs: Long? = null
        private set
    var damaged = 0
        private set
    private fun line(): String? {
        val text = StringBuilder()
        while (true) {
            val ch = reader.read()
            if (ch < 0) return text.toString().takeIf { it.isNotEmpty() }
            if (ch == 10) return text.toString().trimEnd('\r')
            require(text.length < 4096) { "Слишком длинная строка CSV" }
            text.append(ch.toChar())
        }
    }
    fun next(): Row? {
        while (true) {
            val line = line() ?: return null
            require(line.length <= 4096) { "Слишком длинная строка CSV" }
            if (line.startsWith("# started_at_unix_ms=")) {
                val value = line.substringAfter('=').trim().toLongOrNull()
                if (startedAtMs == null && value != null && value in 1704067200000L until 4102444800000L) startedAtMs = value
            }
            if (line.isBlank() || line.startsWith('#')) continue
            val fields = line.split(',')
            if (columns == null) {
                require(fields.first() == "millis_ms") { "Неизвестный формат CSV" }
                require("signal_ok" in fields || ("hr_valid" in fields && "spo2_valid" in fields)) { "CSV без признаков достоверности" }
                columns = fields.withIndex().associate { it.value to it.index }
                continue
            }
            val cols = columns!!
            if (fields.size != cols.size) { damaged++; continue }
            fun cell(key: String) = cols[key]?.let { fields.getOrNull(it) }
            fun number(key: String) = cell(key)?.toFloatOrNull()?.takeIf { it.isFinite() }
            fun bpm(key: String) = number(key)?.takeIf { it in 20f..250f }?.roundToInt()
            val ms = cell("millis_ms")?.toLongOrNull()?.takeIf { it in 0..0xffffffffL }
            if (ms == null) { damaged++; continue }
            val kind = cell("kind")
            val ecg = if (kind == "ecg" && cell("signal_ok") == "1") number("filtered_relative") else null
            val hr = when {
                kind == "ecg" && cell("signal_ok") == "1" -> bpm("ecg_bpm")
                kind == "ppg" && cell("ppg_valid") == "1" -> bpm("ppg_bpm")
                kind == null && cell("hr_valid") == "1" -> bpm("bpm")
                else -> null
            }
            val oxygen = if (cell("spo2_valid") == "1") number("spo2")?.takeIf { it in 1f..100f }?.roundToInt() else null
            return Row(ms, ecg, hr, oxygen)
        }
    }
    fun hasHeader() = columns != null
}
