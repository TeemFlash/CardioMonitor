package com.cardiomonitor.app.ui.navigation

import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.cardiomonitor.app.ui.history.HistoryScreen
import com.cardiomonitor.app.ui.history.HistoryViewModel
import com.cardiomonitor.app.ui.live.LiveMonitorScreen
import com.cardiomonitor.app.ui.live.LiveMonitorViewModel

sealed class Screen(val route: String, val label: String) {
    data object Live : Screen("live", "Мониторинг")
    data object History : Screen("history", "История")
    data object SessionDetail : Screen("session_detail/{sessionId}", "Детали")
}

@Composable
fun AppNavigation(
    liveViewModel: LiveMonitorViewModel,
    historyViewModel: HistoryViewModel
) {
    val navController = rememberNavController()

    Scaffold(
        bottomBar = { BottomBar(navController) }
    ) { padding ->
        NavHost(
            navController = navController,
            startDestination = Screen.Live.route,
            modifier = androidx.compose.ui.Modifier.padding(padding)
        ) {
            composable(Screen.Live.route) {
                LiveMonitorScreen(liveViewModel)
            }
            composable(Screen.History.route) {
                HistoryScreen(historyViewModel) { sessionId ->
                    navController.navigate("session_detail/$sessionId")
                }
            }
            composable(Screen.SessionDetail.route) { backStackEntry ->
                val sessionId = backStackEntry.arguments?.getString("sessionId")
                // Детали сессии подключаются здесь через SessionDetailViewModel (см. отдельный файл)
            }
        }
    }
}

@Composable
private fun BottomBar(navController: NavHostController) {
    val currentEntry by navController.currentBackStackEntryAsState()
    val currentRoute = currentEntry?.destination?.route

    NavigationBar {
        NavigationBarItem(
            selected = currentRoute == Screen.Live.route,
            onClick = { navController.navigate(Screen.Live.route) },
            icon = { Icon(Icons.Filled.Favorite, contentDescription = null) },
            label = { Text(Screen.Live.label) }
        )
        NavigationBarItem(
            selected = currentRoute == Screen.History.route,
            onClick = { navController.navigate(Screen.History.route) },
            icon = { Icon(Icons.Filled.Refresh, contentDescription = null) },
            label = { Text(Screen.History.label) }
        )
    }
}
