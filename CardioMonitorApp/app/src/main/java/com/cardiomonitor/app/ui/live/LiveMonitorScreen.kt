package com.cardiomonitor.app.ui.live

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cardiomonitor.app.domain.model.DeviceConnectionState
import com.cardiomonitor.app.ui.components.*

@Composable
fun LiveMonitorScreen(viewModel: LiveMonitorViewModel, onRecords: () -> Unit, onDevice: () -> Unit) {
    val state by viewModel.connectionState.collectAsState()
    val ecg by viewModel.ecgWindow.collectAsState()
    val bpm by viewModel.currentBpm.collectAsState()
    val spo2 by viewModel.currentSpo2.collectAsState()
    val connected = state is DeviceConnectionState.Connected
    val last = listOfNotNull(bpm?.timestampMs, spo2?.timestampMs, ecg.lastOrNull()?.timestampMs).maxOrNull()
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(18.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Icon(Icons.Default.Favorite, null, tint = MedicalColors.Teal, modifier = Modifier.size(21.dp))
                Text("CARDIO MONITOR", letterSpacing = 1.5.sp, fontSize = 12.sp, fontWeight = FontWeight.Bold,
                    color = MedicalColors.Teal)
            }
            Text("ПРОТОТИП", fontSize = 10.sp, color = MedicalColors.Muted)
        }
        SectionTitle("Ваши показатели", if (last == null) "Измерений пока нет" else "Получены данные • время прибора")
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            MetricTile("Пульс", if (connected) bpm?.bpm?.toString() ?: "—" else "—", "уд/мин", MedicalColors.Pulse, Modifier.weight(1f))
            MetricTile("SpO₂", if (connected) spo2?.percentage?.toString() ?: "—" else "—", "% насыщения", MedicalColors.Oxygen, Modifier.weight(1f))
        }
        MedicalCard {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                Text("Электрокардиограмма", fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
                Text("ЭКГ", fontSize = 11.sp, color = MedicalColors.Teal)
            }
            EcgPaper(if (connected) ecg else emptyList(), Modifier.fillMaxWidth().height(145.dp))
            Text(if (connected && ecg.isNotEmpty()) "Качество сигнала не передаётся" else "Для графика нужны данные прибора",
                color = MedicalColors.Muted, fontSize = 12.sp)
            Text("Относительная амплитуда • масштаб не калиброван", fontSize = 11.sp, color = MedicalColors.Muted)
        }
        MedicalCard {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Column { Text("Ваш прибор", fontWeight = FontWeight.SemiBold); Text("ESP32-C3 · CardioMonitor", fontSize = 12.sp, color = MedicalColors.Muted) }
                StatusPill(if (connected) "На связи" else "Нет связи", connected)
            }
            InfoRow("Заряд прибора", "Нет данных")
            TextButton(onClick = onDevice, contentPadding = PaddingValues(0.dp)) { Text("Открыть устройство →") }
        }
        Button(onClick = onRecords, modifier = Modifier.fillMaxWidth().height(50.dp), shape = androidx.compose.foundation.shape.RoundedCornerShape(14.dp)) {
            Text("Все записи")
        }
        Text("Обмен с прибором ещё не реализован. Показания появятся после подключения передачи данных.",
            color = MedicalColors.Muted, fontSize = 12.sp, lineHeight = 18.sp)
    }
}
