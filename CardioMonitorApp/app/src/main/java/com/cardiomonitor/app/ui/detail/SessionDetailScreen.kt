package com.cardiomonitor.app.ui.detail

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import com.cardiomonitor.app.data.repository.AnalysisRepository
import com.cardiomonitor.app.domain.model.AnalysisSession
import kotlinx.coroutines.launch

class SessionDetailViewModel(
    private val repository: AnalysisRepository
) : ViewModel() {
    var session by mutableStateOf<AnalysisSession?>(null)
        private set

    fun load(sessionId: String, scope: kotlinx.coroutines.CoroutineScope) {
        scope.launch { session = repository.getFullSession(sessionId) }
    }
}

@Composable
fun SessionDetailScreen(session: AnalysisSession?) {
    if (session == null) {
        Box(modifier = Modifier.fillMaxSize(), contentAlignment = androidx.compose.ui.Alignment.Center) {
            CircularProgressIndicator()
        }
        return
    }

    Column(modifier = Modifier.fillMaxSize().padding(16.dp)) {
        Text("Анализ от ${java.text.SimpleDateFormat("dd.MM.yyyy HH:mm").format(java.util.Date(session.startedAtMs))}",
            style = MaterialTheme.typography.titleLarge)

        Spacer(Modifier.height(16.dp))

        Row(horizontalArrangement = Arrangement.SpaceBetween, modifier = Modifier.fillMaxWidth()) {
            Text("Средний пульс: ${session.averageBpm ?: "--"} уд/мин")
            Text("Средний SpO₂: ${session.averageSpo2 ?: "--"}%")
        }

        Spacer(Modifier.height(16.dp))
        Text("ЭКГ (${session.ecgSamples.size} точек)", style = MaterialTheme.typography.titleMedium)
        // TODO: полноценный график всей сессии — рекомендуется MPAndroidChart
        // с прокруткой/зумом, т.к. точек может быть очень много.

        Spacer(Modifier.height(16.dp))
        Text("История измерений пульса: ${session.pulseReadings.size} записей")
        Text("История измерений SpO₂: ${session.spo2Readings.size} записей")
    }
}
