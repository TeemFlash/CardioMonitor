package com.cardiomonitor.app.ui.navigation

import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.List
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.navigation.compose.*
import com.cardiomonitor.app.data.repository.AnalysisRepository
import com.cardiomonitor.app.ui.components.MedicalColors
import com.cardiomonitor.app.ui.history.*
import com.cardiomonitor.app.ui.live.*
import com.cardiomonitor.app.ui.device.DeviceScreen
import com.cardiomonitor.app.ui.detail.SessionDetailScreen

@Composable
fun AppNavigation(live: LiveMonitorViewModel, history: HistoryViewModel, repository: AnalysisRepository) {
    val nav = rememberNavController()
    val entry by nav.currentBackStackEntryAsState()
    val route = entry?.destination?.route
    fun navigate(target: String) {
        nav.navigate(target) {
            popUpTo("live") { saveState = true }
            launchSingleTop = true; restoreState = true
        }
    }
    Scaffold(containerColor = MedicalColors.Background, bottomBar = {
        if (route != "session_detail/{sessionId}") NavigationBar(containerColor = Color.White, tonalElevation = androidx.compose.ui.unit.Dp(0f)) {
            val tabs = listOf(Triple("live","Главная",Icons.Default.Home), Triple("history","Записи",Icons.Default.List), Triple("device","Устройство",Icons.Default.Settings))
            tabs.forEach { (target,label,icon) ->
                NavigationBarItem(selected = route == target, onClick = { navigate(target) },
                    icon = { Icon(icon, contentDescription = null) }, label = { Text(label) },
                    colors = NavigationBarItemDefaults.colors(selectedIconColor = MedicalColors.Teal,
                        selectedTextColor = MedicalColors.Teal, indicatorColor = MedicalColors.TealSoft,
                        unselectedIconColor = MedicalColors.Muted, unselectedTextColor = MedicalColors.Muted))
            }
        }
    }) { padding ->
        NavHost(nav, startDestination = "live", modifier = Modifier.padding(padding)) {
            composable("live") { LiveMonitorScreen(live, { navigate("history") }, { navigate("device") }) }
            composable("history") { HistoryScreen(history) { nav.navigate("session_detail/${android.net.Uri.encode(it)}") } }
            composable("device") { DeviceScreen(live) }
            composable("session_detail/{sessionId}") { detail ->
                SessionDetailScreen(detail.arguments?.getString("sessionId") ?: "", repository) { nav.popBackStack() }
            }
        }
    }
}
