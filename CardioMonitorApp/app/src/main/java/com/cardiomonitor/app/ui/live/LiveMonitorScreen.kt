package com.cardiomonitor.app.ui.live

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.collectAsState
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.cardiomonitor.app.domain.model.DeviceConnectionState

@Composable
fun LiveMonitorScreen(viewModel: LiveMonitorViewModel) {
    val connectionState by viewModel.connectionState.collectAsState()
    val ecgWindow by viewModel.ecgWindow.collectAsState()
    val bpm by viewModel.currentBpm.collectAsState()
    val spo2 by viewModel.currentSpo2.collectAsState()

    Column(modifier = Modifier.fillMaxSize().padding(16.dp)) {

        ConnectionBadge(connectionState)

        Spacer(Modifier.height(16.dp))

        Text("ЭКГ", style = MaterialTheme.typography.titleMedium)
        EcgWaveform(
            values = ecgWindow.map { it.value },
            modifier = Modifier.fillMaxWidth().height(180.dp)
        )

        Spacer(Modifier.height(24.dp))

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            MetricCard(title = "Пульс", value = bpm?.bpm?.toString() ?: "--", unit = "уд/мин")
            MetricCard(title = "SpO₂", value = spo2?.percentage?.toString() ?: "--", unit = "%")
        }
    }
}

@Composable
private fun ConnectionBadge(state: DeviceConnectionState) {
    val (text, color) = when (state) {
        is DeviceConnectionState.Connected -> "Подключено к устройству" to Color(0xFF2E7D32)
        is DeviceConnectionState.Connecting -> "Подключение..." to Color(0xFFF9A825)
        is DeviceConnectionState.Error -> "Ошибка: ${state.message}" to Color(0xFFC62828)
        DeviceConnectionState.Disconnected -> "Не подключено" to Color(0xFF757575)
    }
    Text(text, color = color, style = MaterialTheme.typography.labelLarge)
}

@Composable
private fun MetricCard(title: String, value: String, unit: String) {
    Card(modifier = Modifier.padding(8.dp).width(140.dp)) {
        Column(
            modifier = Modifier.padding(16.dp),
            horizontalAlignment = androidx.compose.ui.Alignment.CenterHorizontally
        ) {
            Text(title, style = MaterialTheme.typography.labelMedium)
            Text(value, style = MaterialTheme.typography.headlineMedium)
            Text(unit, style = MaterialTheme.typography.labelSmall)
        }
    }
}

/**
 * Простая отрисовка волны ЭКГ на Canvas.
 * Для продакшена можно заменить на MPAndroidChart LineChart для лучшей производительности
 * на длинных сессиях, но для live-окна в 500 точек Canvas вполне достаточно.
 */
@Composable
private fun EcgWaveform(values: List<Float>, modifier: Modifier = Modifier) {
    Canvas(modifier = modifier) {
        if (values.size < 2) return@Canvas
        val maxV = values.maxOrNull() ?: 1f
        val minV = values.minOrNull() ?: 0f
        val range = (maxV - minV).takeIf { it != 0f } ?: 1f
        val stepX = size.width / (values.size - 1)

        val path = androidx.compose.ui.graphics.Path()
        values.forEachIndexed { index, v ->
            val x = index * stepX
            val y = size.height - ((v - minV) / range) * size.height
            if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        drawPath(path, color = Color(0xFFD32F2F), style = androidx.compose.ui.graphics.drawscope.Stroke(width = 3f))
    }
}
