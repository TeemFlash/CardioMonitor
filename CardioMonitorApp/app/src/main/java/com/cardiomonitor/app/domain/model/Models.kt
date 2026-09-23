package com.cardiomonitor.app.domain.model

/**
 * Один сэмпл ЭКГ-волны (приходит потоком, в бинарном виде с устройства)
 */
data class EcgSample(
    val timestampMs: Long,
    val value: Float // мкВ или нормализованное значение — зависит от прошивки
)

/**
 * Разовое измерение пульса
 */
data class PulseReading(
    val timestampMs: Long,
    val bpm: Int
)

/**
 * Разовое измерение уровня кислорода в крови
 */
data class SpO2Reading(
    val timestampMs: Long,
    val percentage: Int
)

/**
 * Сессия анализа целиком — то, что попадает в историю.
 * Одна сессия = один "снятый анализ" (пациент подключил датчик и снял показания).
 */
data class AnalysisSession(
    val id: String,
    val startedAtMs: Long,
    val endedAtMs: Long?,
    val deviceId: String,
    val ecgSamples: List<EcgSample>,
    val pulseReadings: List<PulseReading>,
    val spo2Readings: List<SpO2Reading>,
    val averageBpm: Int?,
    val averageSpo2: Int?,
    val note: String? = null
)

/**
 * Статус подключения к устройству — для UI live-экрана
 */
sealed class DeviceConnectionState {
    data object Disconnected : DeviceConnectionState()
    data object Connecting : DeviceConnectionState()
    data class Connected(val deviceId: String) : DeviceConnectionState()
    data class Error(val message: String) : DeviceConnectionState()
}
