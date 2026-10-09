package com.cardiomonitor.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.toArgb
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import com.cardiomonitor.app.ui.components.MedicalColors
import com.cardiomonitor.app.ui.components.MedicalTheme
import com.cardiomonitor.app.ui.history.HistoryViewModel
import com.cardiomonitor.app.ui.live.LiveMonitorViewModel
import com.cardiomonitor.app.ui.navigation.AppNavigation
import androidx.core.view.WindowCompat

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = MedicalColors.Background.toArgb()
        window.navigationBarColor = android.graphics.Color.WHITE
        WindowCompat.getInsetsController(window, window.decorView).apply {
            isAppearanceLightStatusBars = true
            isAppearanceLightNavigationBars = true
        }
        val repository = (application as CardioMonitorApp).repository
        setContent {
            val factory = remember {
                object : ViewModelProvider.Factory {
                    @Suppress("UNCHECKED_CAST")
                    override fun <T : ViewModel> create(modelClass: Class<T>): T = when (modelClass) {
                        LiveMonitorViewModel::class.java -> LiveMonitorViewModel(repository) as T
                        HistoryViewModel::class.java -> HistoryViewModel(repository) as T
                        else -> error("Unknown view model")
                    }
                }
            }
            val live: LiveMonitorViewModel = viewModel(factory = factory)
            val history: HistoryViewModel = viewModel(factory = factory)
            MedicalTheme { AppNavigation(live, history, repository) }
        }
    }
}
@Composable fun AppTheme(content: @Composable () -> Unit) = MedicalTheme(content)
