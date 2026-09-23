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

        // TODO: host/port/сертификат — из настроек "спаривания" с устройством,
        // а не хардкод. Например, экран "Подключить устройство" с QR-кодом.
        val connectionManager = DeviceConnectionManager(
            host = "192.168.4.1",
            port = 8443,
            trustedCertPem = "" // сертификат устройства, полученный при спаривании
        )

        repository = AnalysisRepository(db.analysisDao(), connectionManager)
    }
}
