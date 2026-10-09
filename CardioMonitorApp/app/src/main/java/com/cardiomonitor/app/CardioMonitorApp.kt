package com.cardiomonitor.app

import android.app.Application
import com.cardiomonitor.app.data.local.AppDatabase
import com.cardiomonitor.app.data.network.DeviceConnectionManager
import com.cardiomonitor.app.data.repository.AnalysisRepository

class CardioMonitorApp : Application() {

    lateinit var repository: AnalysisRepository
        private set

    override fun onCreate() {
        super.onCreate()

        // TODO: заменить на реальный passphrase из SecureKeyProvider (Android Keystore)
        val dbPassphrase = "REPLACE_WITH_KEYSTORE_DERIVED_KEY".toByteArray()
        val db = AppDatabase.getInstance(this, dbPassphrase)

        val connectionManager = DeviceConnectionManager(this)
        repository = AnalysisRepository(db, connectionManager)
    }
}
