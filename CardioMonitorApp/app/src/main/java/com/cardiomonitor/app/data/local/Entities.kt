package com.cardiomonitor.app.data.local

import androidx.room.Entity
import androidx.room.PrimaryKey
import androidx.room.ForeignKey
import androidx.room.Index

@Entity(tableName = "sessions")
data class SessionEntity(
    @PrimaryKey val id: String,
    val startedAtMs: Long,
    val endedAtMs: Long?,
    val deviceId: String,
    val averageBpm: Int?,
    val averageSpo2: Int?,
    val note: String?
)

@Entity(
    tableName = "ecg_samples",
    foreignKeys = [ForeignKey(
        entity = SessionEntity::class,
        parentColumns = ["id"],
        childColumns = ["sessionId"],
        onDelete = ForeignKey.CASCADE
    )],
    indices = [Index("sessionId")]
)
data class EcgSampleEntity(
    @PrimaryKey(autoGenerate = true) val rowId: Long = 0,
    val sessionId: String,
    val timestampMs: Long,
    val value: Float
)

@Entity(
    tableName = "pulse_readings",
    foreignKeys = [ForeignKey(
        entity = SessionEntity::class,
        parentColumns = ["id"],
        childColumns = ["sessionId"],
        onDelete = ForeignKey.CASCADE
    )],
    indices = [Index("sessionId")]
)
data class PulseReadingEntity(
    @PrimaryKey(autoGenerate = true) val rowId: Long = 0,
    val sessionId: String,
    val timestampMs: Long,
    val bpm: Int
)

@Entity(
    tableName = "spo2_readings",
    foreignKeys = [ForeignKey(
        entity = SessionEntity::class,
        parentColumns = ["id"],
        childColumns = ["sessionId"],
        onDelete = ForeignKey.CASCADE
    )],
    indices = [Index("sessionId")]
)
data class Spo2ReadingEntity(
    @PrimaryKey(autoGenerate = true) val rowId: Long = 0,
    val sessionId: String,
    val timestampMs: Long,
    val percentage: Int
)
