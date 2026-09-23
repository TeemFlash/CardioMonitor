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

    @Query("SELECT * FROM sessions ORDER BY startedAtMs DESC")
    fun observeAllSessions(): Flow<List<SessionEntity>>

    @Query("SELECT * FROM sessions WHERE id = :sessionId")
    suspend fun getSession(sessionId: String): SessionEntity?

    @Query("SELECT * FROM ecg_samples WHERE sessionId = :sessionId ORDER BY timestampMs ASC")
    suspend fun getEcgSamples(sessionId: String): List<EcgSampleEntity>

    @Query("SELECT * FROM pulse_readings WHERE sessionId = :sessionId ORDER BY timestampMs ASC")
    suspend fun getPulseReadings(sessionId: String): List<PulseReadingEntity>

    @Query("SELECT * FROM spo2_readings WHERE sessionId = :sessionId ORDER BY timestampMs ASC")
    suspend fun getSpo2Readings(sessionId: String): List<Spo2ReadingEntity>

    @Query("DELETE FROM sessions WHERE id = :sessionId")
    suspend fun deleteSession(sessionId: String)
}
