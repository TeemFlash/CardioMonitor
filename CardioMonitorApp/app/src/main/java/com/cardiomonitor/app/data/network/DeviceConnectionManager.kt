package com.cardiomonitor.app.data.network

import com.cardiomonitor.app.domain.model.DeviceConnectionState
import com.cardiomonitor.app.domain.model.EcgSample
import com.cardiomonitor.app.domain.model.PulseReading
import com.cardiomonitor.app.domain.model.SpO2Reading
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import org.json.JSONObject
import java.io.DataInputStream
import java.net.InetSocketAddress
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory

/**
 * Протокол пакета (см. описание в README):
 * [1 байт: тип пакета][4 байта: длина payload][payload]
 *   type 0x01 = JSON control message (UTF-8 текст)
 *   type 0x02 = бинарный чанк ЭКГ: N x (8 байт timestamp Int64 + 4 байта value Float32)
 *
 * Всё соединение — поверх TLS. Сертификат устройства должен быть заранее
 * доверен приложением (pinning) — НЕ используем системный trust store,
 * чтобы исключить MITM в локальной сети.
 */
sealed class IncomingEvent {
    data class Ecg(val samples: List<EcgSample>) : IncomingEvent()
    data class Pulse(val reading: PulseReading) : IncomingEvent()
    data class Spo2(val reading: SpO2Reading) : IncomingEvent()
    data class SessionStarted(val sessionId: String, val deviceId: String) : IncomingEvent()
    data class SessionEnded(val sessionId: String) : IncomingEvent()
}

class DeviceConnectionManager(
    private val host: String,
    private val port: Int,
    private val trustedCertPem: String // сертификат устройства, зашитый/полученный при "спаривании"
) {
    private val _connectionState = MutableStateFlow<DeviceConnectionState>(DeviceConnectionState.Disconnected)
    val connectionState: StateFlow<DeviceConnectionState> = _connectionState

    private val eventChannel = Channel<IncomingEvent>(capacity = Channel.BUFFERED)
    fun events() = eventChannel.receiveAsFlow()

    private var socket: SSLSocket? = null

    /**
     * Запускается в отдельной корутине (Dispatchers.IO) со стороны репозитория.
     * Блокирующий цикл чтения — нормально для сокет-потока.
     */
    fun connectAndListen() {
        _connectionState.value = DeviceConnectionState.Connecting
        try {
            val sslContext = buildPinnedSslContext(trustedCertPem)
            val factory: SSLSocketFactory = sslContext.socketFactory
            val sslSocket = factory.createSocket() as SSLSocket
            sslSocket.connect(InetSocketAddress(host, port), 5000)
            sslSocket.startHandshake()
            socket = sslSocket

            _connectionState.value = DeviceConnectionState.Connected(deviceId = host)

            val input = DataInputStream(sslSocket.inputStream)
            while (!sslSocket.isClosed) {
                val packetType = input.readUnsignedByte()
                val length = input.readInt()
                val payload = ByteArray(length)
                input.readFully(payload)

                when (packetType) {
                    0x01 -> handleControlPacket(payload)
                    0x02 -> handleEcgChunk(payload)
                    else -> { /* неизвестный тип пакета — игнорируем, логируем при необходимости */ }
                }
            }
        } catch (e: Exception) {
            _connectionState.value = DeviceConnectionState.Error(e.message ?: "Connection error")
        } finally {
            disconnect()
        }
    }

    private fun handleControlPacket(payload: ByteArray) {
        val json = JSONObject(String(payload, Charsets.UTF_8))
        when (json.optString("type")) {
            "session_started" -> eventChannel.trySend(
                IncomingEvent.SessionStarted(
                    sessionId = json.getString("session_id"),
                    deviceId = json.getString("device_id")
                )
            )
            "session_ended" -> eventChannel.trySend(
                IncomingEvent.SessionEnded(sessionId = json.getString("session_id"))
            )
            "pulse" -> eventChannel.trySend(
                IncomingEvent.Pulse(
                    PulseReading(
                        timestampMs = json.getLong("ts"),
                        bpm = json.getInt("bpm")
                    )
                )
            )
            "spo2" -> eventChannel.trySend(
                IncomingEvent.Spo2(
                    SpO2Reading(
                        timestampMs = json.getLong("ts"),
                        percentage = json.getInt("value")
                    )
                )
            )
        }
    }

    private fun handleEcgChunk(payload: ByteArray) {
        // Каждый сэмпл: 8 байт timestamp (Int64, big-endian) + 4 байта value (Float32, big-endian)
        val buffer = java.nio.ByteBuffer.wrap(payload)
        val samples = mutableListOf<EcgSample>()
        while (buffer.remaining() >= 12) {
            val ts = buffer.long
            val value = buffer.float
            samples.add(EcgSample(timestampMs = ts, value = value))
        }
        eventChannel.trySend(IncomingEvent.Ecg(samples))
    }

    fun disconnect() {
        socket?.close()
        socket = null
        _connectionState.value = DeviceConnectionState.Disconnected
    }

    private fun buildPinnedSslContext(certPem: String): SSLContext {
        // TODO: реализовать pinning — загрузить certPem в собственный TrustManager,
        // чтобы принимать соединение ТОЛЬКО от вашего устройства, а не от любого TLS-сервера
        // в сети. Это ключевой момент безопасности для мед. данных.
        return SSLContext.getInstance("TLS").apply { init(null, null, null) }
    }
}
