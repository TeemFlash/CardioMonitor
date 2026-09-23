package com.cardiomonitor.app.data.local

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase
import net.sqlcipher.database.SQLiteDatabase
import net.sqlcipher.database.SupportFactory

@Database(
    entities = [
        SessionEntity::class,
        EcgSampleEntity::class,
        PulseReadingEntity::class,
        Spo2ReadingEntity::class
    ],
    version = 1,
    exportSchema = true
)
abstract class AppDatabase : RoomDatabase() {
    abstract fun analysisDao(): AnalysisDao

    companion object {
        @Volatile private var INSTANCE: AppDatabase? = null

        /**
         * passphrase должен браться из EncryptedSharedPreferences / Android Keystore,
         * а не хардкодиться. См. SecureKeyProvider (TODO ниже) — генерирует
         * и хранит ключ шифрования БД в защищённом Keystore, недоступном
         * другим приложениям и без root/эксплойта.
         */
        fun getInstance(context: Context, passphrase: ByteArray): AppDatabase {
            return INSTANCE ?: synchronized(this) {
                SQLiteDatabase.loadLibs(context)
                val factory = SupportFactory(passphrase)
                Room.databaseBuilder(
                    context.applicationContext,
                    AppDatabase::class.java,
                    "cardio_monitor_encrypted.db"
                )
                    .openHelperFactory(factory)
                    .build()
                    .also { INSTANCE = it }
            }
        }
    }
}

/**
 * TODO: SecureKeyProvider — генерирует AES-ключ в Android Keystore при первом запуске,
 * далее использует его как passphrase для SQLCipher. Ключ никогда не покидает Keystore
 * в открытом виде и не хранится в коде/файлах приложения.
 */
