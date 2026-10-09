package com.cardiomonitor.app.ui.history

import androidx.compose.foundation.clickable
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModel
import com.cardiomonitor.app.data.local.SessionEntity
import com.cardiomonitor.app.data.repository.AnalysisRepository
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.stateIn
import java.text.SimpleDateFormat
import java.util.*

class HistoryViewModel(
    repository: AnalysisRepository,
    scope: kotlinx.coroutines.CoroutineScope
) : ViewModel() {
    val sessions = repository.observeHistory()
        .stateIn(scope, SharingStarted.WhileSubscribed(5000), emptyList())
}

@Composable
fun HistoryScreen(
    viewModel: HistoryViewModel,
    onSessionClick: (String) -> Unit
) {
    val sessions by viewModel.sessions.collectAsState()
    val dateFormat = remember { SimpleDateFormat("dd.MM.yyyy HH:mm", Locale.getDefault()) }

    if (sessions.isEmpty()) {
        Box(modifier = Modifier.fillMaxSize(), contentAlignment = androidx.compose.ui.Alignment.Center) {
            Text("История анализов пуста")
        }
        return
    }

    LazyColumn(modifier = Modifier.fillMaxSize().padding(8.dp)) {
        items(sessions) { session ->
            SessionRow(session, dateFormat) { onSessionClick(session.id) }
        }
    }
}

@Composable
private fun SessionRow(
    session: SessionEntity,
    dateFormat: SimpleDateFormat,
    onClick: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
            .clickable(onClick = onClick)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(dateFormat.format(Date(session.startedAtMs)), style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(4.dp))
            Row {
                Text("Пульс: ${session.averageBpm ?: "--"} уд/мин")
                Spacer(Modifier.width(16.dp))
                Text("SpO₂: ${session.averageSpo2 ?: "--"}%")
            }
        }
    }
}
