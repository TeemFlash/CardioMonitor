package com.cardiomonitor.app.data.local

import androidx.room.*
import kotlinx.coroutines.flow.Flow

@Dao
interface AnalysisDao {

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertSession(session: SessionEntity)

    @Update
    suspend fun updateSession(session: SessionEntity)

    @Insert
    suspend fun insertEcgSamples(samples: List<EcgSampleEntity>)

    @Insert
    suspend fun insertPulseReading(reading: PulseReadingEntity)

    @Insert
    suspend fun insertSpo2Reading(reading: Spo2ReadingEntity)

    @Insert
    suspend fun insertPulseReadings(readings: List<PulseReadingEntity>)

    @Insert
    suspend fun insertSpo2Readings(readings: List<Spo2ReadingEntity>)

    @Query("SELECT * FROM sessions ORDER BY startedAtMs DESC")
    fun observeAllSessions(): Flow<List<SessionEntity>>

    @Query("SELECT * FROM sessions WHERE id = :sessionId")
    suspend fun getSession(sessionId: String): SessionEntity?

    @Query("SELECT * FROM ecg_samples WHERE sessionId = :sessionId ORDER BY timestampMs ASC")
    suspend fun getEcgSamples(sessionId: String): List<EcgSampleEntity>

    @Query("SELECT * FROM ecg_samples WHERE sessionId = :sessionId AND timestampMs BETWEEN :start AND :end ORDER BY timestampMs ASC LIMIT 2000")
    suspend fun getEcgWindow(sessionId: String, start: Long, end: Long): List<EcgSampleEntity>

    @Query("SELECT MAX(timestampMs) FROM ecg_samples WHERE sessionId = :sessionId")
    suspend fun getEcgEnd(sessionId: String): Long?

    @Query("SELECT * FROM pulse_readings WHERE sessionId = :sessionId ORDER BY timestampMs ASC")
    suspend fun getPulseReadings(sessionId: String): List<PulseReadingEntity>

    @Query("SELECT * FROM spo2_readings WHERE sessionId = :sessionId ORDER BY timestampMs ASC")
    suspend fun getSpo2Readings(sessionId: String): List<Spo2ReadingEntity>

    @Query("DELETE FROM sessions WHERE id = :sessionId")
    suspend fun deleteSession(sessionId: String)
}
