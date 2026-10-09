package com.cardiomonitor.app.data.repository

import androidx.room.withTransaction
import com.cardiomonitor.app.data.local.*
import com.cardiomonitor.app.data.network.DeviceConnectionManager
import com.cardiomonitor.app.data.importer.SdCsvReader
import com.cardiomonitor.app.domain.model.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.io.File

class AnalysisRepository(
    private val db: AppDatabase,
    private val connectionManager: DeviceConnectionManager
) {
    private val dao = db.analysisDao()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    val snapshot = connectionManager.snapshot.asStateFlow()
    val connectionState = connectionManager.connectionState
    val syncStatus = connectionManager.syncStatus.asStateFlow()
    val livePulse = snapshot.map { it?.bpm?.let { bpm -> PulseReading(System.currentTimeMillis(), bpm) } }
        .stateIn(scope, SharingStarted.Eagerly, null)
    val liveSpo2 = snapshot.map { it?.spo2?.let { value -> SpO2Reading(System.currentTimeMillis(), value) } }
        .stateIn(scope, SharingStarted.Eagerly, null)

    init { connectionManager.start(scope, ::importArchive) }
    fun syncNow() = connectionManager.syncNow()

    private suspend fun importArchive(device: String, name: String, file: File) {
        val id = "$device/$name"
        // Unknown older layouts still appear in history and keep an exact, exportable CSV.
        val known = try { file.bufferedReader().use { val parser = SdCsvReader(it); parser.next(); parser.hasHeader() } }
            catch (e: IllegalArgumentException) { false }
        if (!known) {
            dao.insertSession(SessionEntity(id, 0, null, device, null, null,
                "$name • исходный CSV сохранён; формат не поддерживается для графика"))
            return
        }
        // One transaction: interruptions cannot leave a half-imported recording.
        db.withTransaction {
            val session = SessionEntity(id, 0, null, device, null, null, name)
            dao.insertSession(session)
            val ecg = ArrayList<EcgSampleEntity>(256)
            val pulse = ArrayList<PulseReadingEntity>(256)
            val oxygen = ArrayList<Spo2ReadingEntity>(256)
            var duration = 0L
            var lastPulse = -1000L
            var lastOxygen = -1000L
            var sumBpm = 0L; var countBpm = 0L
            var sumSpo2 = 0L; var countSpo2 = 0L
            file.bufferedReader().use { reader ->
                val csv = SdCsvReader(reader)
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val row = csv.next() ?: break
                    duration = maxOf(duration, row.ms)
                    row.ecg?.let { ecg.add(EcgSampleEntity(sessionId = id, timestampMs = row.ms, value = it)) }
                    if (row.bpm != null && row.ms - lastPulse >= 1000) {
                        pulse.add(PulseReadingEntity(sessionId = id, timestampMs = row.ms, bpm = row.bpm))
                        lastPulse = row.ms; sumBpm += row.bpm; countBpm++
                    }
                    if (row.spo2 != null && row.ms - lastOxygen >= 1000) {
                        oxygen.add(Spo2ReadingEntity(sessionId = id, timestampMs = row.ms, percentage = row.spo2))
                        lastOxygen = row.ms; sumSpo2 += row.spo2; countSpo2++
                    }
                    if (ecg.size >= 256) { dao.insertEcgSamples(ecg); ecg.clear() }
                    if (pulse.size >= 256) { dao.insertPulseReadings(pulse); pulse.clear() }
                    if (oxygen.size >= 256) { dao.insertSpo2Readings(oxygen); oxygen.clear() }
                }
                require(csv.hasHeader()) { "В CSV нет заголовка" }
                dao.insertEcgSamples(ecg); dao.insertPulseReadings(pulse); dao.insertSpo2Readings(oxygen)
                dao.updateSession(session.copy(endedAtMs = duration,
                    averageBpm = if (countBpm > 0) ((sumBpm + countBpm / 2) / countBpm).toInt() else null,
                    averageSpo2 = if (countSpo2 > 0) ((sumSpo2 + countSpo2 / 2) / countSpo2).toInt() else null,
                    note = "$name • дата не записана прибором • повреждённых строк: ${csv.damaged}"))
            }
        }
    }

    fun observeHistory(): Flow<List<SessionEntity>> = dao.observeAllSessions()

    suspend fun getFullSession(sessionId: String): AnalysisSession? {
        val session = dao.getSession(sessionId) ?: return null
        val ecg = dao.getEcgWindow(sessionId, 0, 4000).map { EcgSample(it.timestampMs, it.value) }
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

    suspend fun ecgEnd(sessionId: String) = dao.getEcgEnd(sessionId) ?: 0L
    suspend fun ecgWindow(sessionId: String, start: Long) = dao.getEcgWindow(sessionId, start, start + 4000)
        .map { EcgSample(it.timestampMs, it.value) }
    suspend fun legacyEcg(sessionId: String) = dao.getEcgSamples(sessionId)

    fun originalCsv(sessionId: String) = connectionManager.archiveFile(sessionId)

    suspend fun deleteSession(sessionId: String) {
        dao.deleteSession(sessionId)
        connectionManager.archiveFile(sessionId)?.delete()
        // Keep the import fingerprint: deleting a phone copy must not resurrect it on next sync.
    }
}
