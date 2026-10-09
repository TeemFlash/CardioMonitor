package com.cardiomonitor.app.ui.history

import androidx.compose.foundation.clickable
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.cardiomonitor.app.data.repository.AnalysisRepository
import com.cardiomonitor.app.ui.components.*
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.stateIn
import java.util.Calendar

class HistoryViewModel(repository: AnalysisRepository) : ViewModel() {
    val sessions = repository.observeHistory().stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), emptyList())
}

private fun groupLabel(ms: Long): String {
    if (ms == 0L) return "microSD · дата неизвестна"
    val today = Calendar.getInstance().apply { set(Calendar.HOUR_OF_DAY,0); set(Calendar.MINUTE,0); set(Calendar.SECOND,0); set(Calendar.MILLISECOND,0) }
    val yesterday = (today.clone() as Calendar).apply { add(Calendar.DAY_OF_YEAR,-1) }
    return when {
        dateLabel(ms,"yyyy-MM-dd") == dateLabel(today.timeInMillis,"yyyy-MM-dd") -> "Сегодня"
        dateLabel(ms,"yyyy-MM-dd") == dateLabel(yesterday.timeInMillis,"yyyy-MM-dd") -> "Вчера"
        else -> dateLabel(ms,"dd MMMM yyyy")
    }
}

@Composable
fun HistoryScreen(viewModel: HistoryViewModel, onSessionClick: (String) -> Unit) {
    val sessions by viewModel.sessions.collectAsState()
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
        item { SectionTitle("Записи", "Сохранённые измерения на телефоне") }
        item { StatusPill("${sessions.size} записей") }
        if (sessions.isEmpty()) item {
            MedicalCard {
                Box(Modifier.fillMaxWidth().height(130.dp), contentAlignment = Alignment.Center) {
                    EcgPaper(emptyList(), Modifier.fillMaxSize(), "Записей пока нет")
                }
                Text("Ваша история начинается здесь", fontWeight = FontWeight.SemiBold)
                Text("Подключите прибор к той же Wi-Fi-сети. Завершённые записи microSD появятся здесь автоматически.",
                    color = MedicalColors.Muted, fontSize = 13.sp, lineHeight = 20.sp)
            }
        }
        sessions.groupBy { groupLabel(it.startedAtMs) }.forEach { (day, records) ->
            item { Text(day, fontWeight = FontWeight.SemiBold, color = MedicalColors.Muted, modifier = Modifier.padding(top = 8.dp)) }
            items(records, key = { it.id }) { session ->
                MedicalCard(Modifier.clickable { onSessionClick(session.id) }) {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text(dateLabel(session.startedAtMs,"HH:mm"), fontSize = 21.sp, fontWeight = FontWeight.SemiBold)
                        Text(durationLabel(session.startedAtMs,session.endedAtMs), fontSize = 13.sp, color = MedicalColors.Muted)
                    }
                    Text(session.note?.substringBefore(" •") ?: "Запись измерений", fontSize = 12.sp, color = MedicalColors.Muted)
                    Row(horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                        Text("♥  ${session.averageBpm ?: "—"} уд/мин", color = MedicalColors.Pulse)
                        Text("SpO₂  ${session.averageSpo2 ?: "—"}%", color = MedicalColors.Oxygen)
                    }
                }
            }
        }
    }
}
