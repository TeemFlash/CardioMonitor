package com.cardiomonitor.app.ui.detail

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cardiomonitor.app.data.repository.AnalysisRepository
import com.cardiomonitor.app.domain.model.AnalysisSession
import com.cardiomonitor.app.domain.model.EcgSample
import com.cardiomonitor.app.ui.components.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

@Composable
fun SessionDetailScreen(id: String, repository: AnalysisRepository, onBack: () -> Unit) {
    var session by remember(id) { mutableStateOf<AnalysisSession?>(null) }
    var ecgEnd by remember(id) { mutableStateOf(0L) }
    var loading by remember(id) { mutableStateOf(true) }
    var message by remember(id) { mutableStateOf<String?>(null) }
    var confirm by remember { mutableStateOf(false) }
    var busy by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    LaunchedEffect(id) {
        try { session = withContext(Dispatchers.IO) { repository.getFullSession(id) }; ecgEnd = repository.ecgEnd(id) }
        catch (e: Exception) { message = "Не удалось прочитать запись" }
        finally { loading = false }
    }
    val exporter = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/csv")) { uri ->
        val record = session
        if (uri != null && record != null) scope.launch {
            busy = true
            try {
                withContext(Dispatchers.IO) {
                    val stream = context.contentResolver.openOutputStream(uri) ?: error("No output stream")
                    val original = repository.originalCsv(id)
                    if (original != null) stream.use { out -> original.inputStream().use { it.copyTo(out) } }
                    else stream.bufferedWriter().use { out ->
                        out.write("kind,timestamp_ms,value\n")
                        repository.legacyEcg(id).forEach { out.write("ecg,${it.timestampMs},${it.value}\n") }
                        record.pulseReadings.forEach { out.write("pulse,${it.timestampMs},${it.bpm}\n") }
                        record.spo2Readings.forEach { out.write("spo2,${it.timestampMs},${it.percentage}\n") }
                    }
                }
                message = "Запись экспортирована"
            } catch (e: Exception) { message = "Не удалось экспортировать запись" }
            finally { busy = false }
        }
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        TextButton(onClick = onBack, contentPadding = PaddingValues(0.dp)) { Text("← Назад к записям") }
        val record = session
        if (loading) { CircularProgressIndicator(); return@Column }
        if (record == null) { SectionTitle("Запись недоступна", message ?: "Она могла быть удалена"); return@Column }
        SectionTitle("Запись измерений", dateLabel(record.startedAtMs))
        record.note?.let { Text(it, fontSize = 12.sp, color = MedicalColors.Muted) }
        StatusPill(durationLabel(record.startedAtMs, record.endedAtMs))
        var position by remember(id) { mutableStateOf(0f) }
        val first = 0L
        val last = ecgEnd
        val span = last
        val start = (position * (span-4000).coerceAtLeast(0)).toLong()
        var visible by remember(id) { mutableStateOf<List<EcgSample>>(emptyList()) }
        LaunchedEffect(id, start) { visible = repository.ecgWindow(id, start) }
        MedicalCard {
            Text("Электрокардиограмма", fontWeight = FontWeight.SemiBold)
            EcgPaper(visible, Modifier.fillMaxWidth().height(205.dp), "В записи нет ЭКГ")
            Slider(value = position, onValueChange = { position = it }, enabled = span > 4000)
            Text("%.1f–%.1f с • относительная амплитуда".format((start-first)/1000f, (minOf(start+4000,last)-first).coerceAtLeast(0)/1000f), fontSize = 11.sp, color = MedicalColors.Muted)
            Text("Показаны достоверные отсчёты; исходные данные — в экспорте CSV", fontSize = 11.sp, color = MedicalColors.Muted)
        }
        MedicalCard {
            Text("Показатели записи", fontWeight = FontWeight.SemiBold)
            Text("Мин. / среднее / макс.", color = MedicalColors.Muted, fontSize = 12.sp)
            val rates = record.pulseReadings.map { it.bpm }
            val oxygen = record.spo2Readings.map { it.percentage }
            fun stats(values: List<Int>) = if (values.isEmpty()) "Нет данных" else "${values.minOrNull()} / ${values.average().roundToInt()} / ${values.maxOrNull()}"
            InfoRow("Пульс, уд/мин", stats(rates))
            InfoRow("SpO₂, %", stats(oxygen))
            InfoRow("Качество сигнала", "Нет данных")
        }
        if (message != null) Text(message!!, fontSize = 13.sp, color = MedicalColors.Muted)
        Button(onClick = { exporter.launch("${record.id.substringAfterLast('/').removeSuffix(".csv")}.csv") }, enabled = !busy, modifier = Modifier.fillMaxWidth().height(48.dp)) { Text("Экспорт записи") }
        OutlinedButton(onClick = { confirm = true }, enabled = !busy, modifier = Modifier.fillMaxWidth().height(48.dp)) { Text("Удалить запись", color = MedicalColors.Pulse) }
    }
    if (confirm) AlertDialog(onDismissRequest = { if (!busy) confirm = false },
        title = { Text("Удалить запись?") }, text = { Text("Она будет удалена из памяти телефона. Файлы на microSD прибора останутся.") },
        confirmButton = { TextButton(enabled = !busy, onClick = {
            scope.launch {
                busy = true
                try { withContext(Dispatchers.IO) { repository.deleteSession(id) }; confirm = false; onBack() }
                catch (e: Exception) { message = "Не удалось удалить запись"; confirm = false }
                finally { busy = false }
            }
        }) { Text("Удалить", color = MedicalColors.Pulse) } },
        dismissButton = { TextButton(enabled = !busy, onClick = { confirm = false }) { Text("Отмена") } })
}
