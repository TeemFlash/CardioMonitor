package com.cardiomonitor.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.lifecycle.lifecycleScope
import com.cardiomonitor.app.ui.history.HistoryViewModel
import com.cardiomonitor.app.ui.live.LiveMonitorViewModel
import com.cardiomonitor.app.ui.navigation.AppNavigation

/**
 * В бета-версии здесь также должна быть проверка BiometricPrompt перед показом
 * данных (см. androidx.biometric) — доступ к истории анализов только после
 * подтверждения отпечатка/Face ID.
 */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val app = application as CardioMonitorApp
        val liveViewModel = LiveMonitorViewModel(app.repository)
        val historyViewModel = HistoryViewModel(app.repository, lifecycleScope)

        setContent {
            AppTheme {
                Surface(modifier = Modifier) {
                    AppNavigation(liveViewModel, historyViewModel)
                }
            }
        }
    }
}

@Composable
fun AppTheme(content: @Composable () -> Unit) {
    MaterialTheme(content = content)
}
