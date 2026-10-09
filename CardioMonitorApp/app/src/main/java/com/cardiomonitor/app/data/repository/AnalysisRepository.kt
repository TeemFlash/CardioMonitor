package com.cardiomonitor.app.data.repository

import com.cardiomonitor.app.data.local.*
import com.cardiomonitor.app.data.network.DeviceConnectionManager
import com.cardiomonitor.app.data.network.IncomingEvent
import com.cardiomonitor.app.domain.model.*
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch

class AnalysisRepository(
    private val dao: AnalysisDao,
    private val connectionManager: DeviceConnectionManager
) {
    // Живые данные текущей сессии — для Live-экрана
    private val _liveEcg = MutableSharedFlow<EcgSample>(extraBufferCapacity = 256)
    val liveEcg: SharedFlow<EcgSample> = _liveEcg

    private val _livePulse = MutableStateFlow<PulseReading?>(null)
    val livePulse: StateFlow<PulseReading?> = _livePulse

    private val _liveSpo2 = MutableStateFlow<SpO2Reading?>(null)
    val liveSpo2: StateFlow<SpO2Reading?> = _liveSpo2

    val connectionState = connectionManager.connectionState

    private var currentSessionId: String? = null

    fun startListening(scope: CoroutineScope) {
        scope.launch(Dispatchers.IO) { connectionManager.connectAndListen() }
        scope.launch(Dispatchers.IO) {
            connectionManager.events().collect { event -> handleEvent(event) }
        }
    }

    private suspend fun handleEvent(event: IncomingEvent) {
        when (event) {
            is IncomingEvent.SessionStarted -> {
                currentSessionId = event.sessionId
                dao.insertSession(
                    SessionEntity(
                        id = event.sessionId,
                        startedAtMs = System.currentTimeMillis(),
                        endedAtMs = null,
                        deviceId = event.deviceId,
                        averageBpm = null,
                        averageSpo2 = null,
                        note = null
                    )
                )
            }
            is IncomingEvent.SessionEnded -> {
                val session = dao.getSession(event.sessionId)
                if (session != null) {
                    dao.updateSession(session.copy(endedAtMs = System.currentTimeMillis()))
                }
                currentSessionId = null
            }
            is IncomingEvent.Ecg -> {
                val sessionId = currentSessionId ?: return
                dao.insertEcgSamples(event.samples.map {
                    EcgSampleEntity(sessionId = sessionId, timestampMs = it.timestampMs, value = it.value)
                })
                event.samples.forEach { _liveEcg.emit(it) }
            }
            is IncomingEvent.Pulse -> {
                val sessionId = currentSessionId
                if (sessionId != null) {
                    dao.insertPulseReading(
                        PulseReadingEntity(sessionId = sessionId, timestampMs = event.reading.timestampMs, bpm = event.reading.bpm)
                    )
                }
                _livePulse.value = event.reading
            }
            is IncomingEvent.Spo2 -> {
                val sessionId = currentSessionId
                if (sessionId != null) {
                    dao.insertSpo2Reading(
                        Spo2ReadingEntity(sessionId = sessionId, timestampMs = event.reading.timestampMs, percentage = event.reading.percentage)
                    )
                }
                _liveSpo2.value = event.reading
            }
        }
    }

    fun observeHistory(): Flow<List<SessionEntity>> = dao.observeAllSessions()

    suspend fun getFullSession(sessionId: String): AnalysisSession? {
        val session = dao.getSession(sessionId) ?: return null
        val ecg = dao.getEcgSamples(sessionId).map { EcgSample(it.timestampMs, it.value) }
        val pulse = dao.getPulseReadings(sessionId).map { PulseReading(it.timestampMs, it.bpm) }
        val spo2 = dao.getSpo2Readings(sessionId).map { SpO2Reading(it.timestampMs, it.percentage) }
        return AnalysisSession(
            id = session.id,
            startedAtMs = session.startedAtMs,
            endedAtMs = session.endedAtMs,
            deviceId = session.deviceId,
            ecgSamples = ecg,
            pulseReadings = pulse,
            spo2Readings = spo2,
            averageBpm = session.averageBpm,
            averageSpo2 = session.averageSpo2,
            note = session.note
        )
    }

    suspend fun deleteSession(sessionId: String) = dao.deleteSession(sessionId)
}
