package com.cardiomonitor.app.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cardiomonitor.app.domain.model.EcgSample
import java.text.SimpleDateFormat
import java.util.*

object MedicalColors {
    val Background = Color(0xFFF3F6F8)
    val Ink = Color(0xFF14202B)
    val Muted = Color(0xFF5B6B78)
    val Teal = Color(0xFF007F79)
    val TealSoft = Color(0xFFE5F3F0)
    val Pulse = Color(0xFFC0392B)
    val Oxygen = Color(0xFF1A5A99)
    val Border = Color(0xFFE1E8EC)
    val Paper = Color(0xFFFFF5F6)
    val Warning = Color(0xFFFFF1C2)
}

@Composable
fun MedicalTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = lightColorScheme(
        primary = MedicalColors.Teal, onPrimary = Color.White,
        primaryContainer = MedicalColors.TealSoft, onPrimaryContainer = MedicalColors.Teal,
        background = MedicalColors.Background, onBackground = MedicalColors.Ink,
        surface = Color.White, onSurface = MedicalColors.Ink,
        surfaceVariant = MedicalColors.Background, onSurfaceVariant = MedicalColors.Muted,
        outline = MedicalColors.Border, error = MedicalColors.Pulse
    ), content = content)
}

@Composable
fun MedicalCard(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Card(modifier = modifier.fillMaxWidth(), shape = RoundedCornerShape(22.dp),
        colors = CardDefaults.cardColors(containerColor = Color.White)) {
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp), content = content)
    }
}

@Composable
fun SectionTitle(title: String, caption: String? = null) {
    Column(verticalArrangement = Arrangement.spacedBy(5.dp)) {
        Text(title, fontSize = 27.sp, lineHeight = 33.sp, fontWeight = FontWeight.SemiBold)
        if (caption != null) Text(caption, fontSize = 13.sp, color = MedicalColors.Muted)
    }
}

@Composable
fun StatusPill(label: String, connected: Boolean = false) {
    Row(Modifier.background(if (connected) MedicalColors.TealSoft else MedicalColors.Background,
        RoundedCornerShape(50)).padding(horizontal = 10.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Canvas(Modifier.size(6.dp)) { drawCircle(if (connected) MedicalColors.Teal else MedicalColors.Muted) }
        Text(label, fontSize = 11.sp, color = if (connected) MedicalColors.Teal else MedicalColors.Muted)
    }
}

@Composable
fun MetricTile(title: String, value: String, unit: String, color: Color, modifier: Modifier = Modifier) {
    MedicalCard(modifier) {
        Text(title, color = color, fontWeight = FontWeight.Medium, fontSize = 14.sp)
        Text(value, color = color, fontWeight = FontWeight.SemiBold, fontSize = 43.sp, lineHeight = 48.sp)
        Text(unit, fontSize = 12.sp, color = MedicalColors.Muted)
    }
}

@Composable
fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = MedicalColors.Muted, fontSize = 13.sp, modifier = Modifier.weight(1f))
        Text(value, fontSize = 13.sp, fontWeight = FontWeight.Medium, modifier = Modifier.weight(1f))
    }
}

/** Decorative paper grid, relative amplitude and actual sample timestamps, not mm/mV. */
@Composable
fun EcgPaper(samples: List<EcgSample>, modifier: Modifier = Modifier, empty: String = "Ожидание сигнала") {
    Box(modifier.background(MedicalColors.Paper, RoundedCornerShape(12.dp)), contentAlignment = Alignment.Center) {
        Canvas(Modifier.fillMaxSize()) {
            val cell = 9.dp.toPx()
            for (i in 0..(size.width / cell).toInt()) drawLine(
                if (i % 5 == 0) Color(0xFFE4AFB9) else Color(0xFFF2D8DE),
                Offset(i * cell, 0f), Offset(i * cell, size.height), if (i % 5 == 0) 1.2f else .6f)
            for (i in 0..(size.height / cell).toInt()) drawLine(
                if (i % 5 == 0) Color(0xFFE4AFB9) else Color(0xFFF2D8DE),
                Offset(0f, i * cell), Offset(size.width, i * cell), if (i % 5 == 0) 1.2f else .6f)
            val valid = samples.filter { it.value.isFinite() }
            if (valid.size >= 2) {
                val first = samples.first().timestampMs
                val duration = (samples.last().timestampMs - first).coerceAtLeast(1)
                val low = valid.minOf { it.value }; val high = valid.maxOf { it.value }
                val range = (high - low).coerceAtLeast(.001f)
                val path = Path(); var previous: EcgSample? = null
                samples.forEach { point ->
                    if (!point.value.isFinite()) { previous = null; return@forEach }
                    val x = ((point.timestampMs - first).toFloat() / duration) * size.width
                    val y = size.height * (.86f - .72f * (point.value - low) / range)
                    val last = previous
                    if (last == null || point.timestampMs - last.timestampMs !in 1..100) path.moveTo(x, y)
                    else path.lineTo(x, y)
                    previous = point
                }
                drawPath(path, MedicalColors.Ink, style = Stroke(1.5.dp.toPx()))
            }
        }
        if (samples.count { it.value.isFinite() } < 2) Text(empty, color = MedicalColors.Muted,
            fontSize = 12.sp, modifier = Modifier.background(MedicalColors.Paper).padding(8.dp))
    }
}

fun dateLabel(ms: Long, pattern: String = "dd MMM yyyy, HH:mm") =
    if (ms == 0L) "Дата неизвестна" else SimpleDateFormat(pattern, Locale("ru")).format(Date(ms))
fun durationLabel(start: Long, end: Long?): String {
    if (end == null) return "Не завершена"
    val seconds = ((end - start) / 1000).coerceAtLeast(0)
    return "%d:%02d".format(seconds / 60, seconds % 60)
}
