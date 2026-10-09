package com.cardiomonitor.app.ui.device

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.cardiomonitor.app.ui.components.*
import com.cardiomonitor.app.ui.live.LiveMonitorViewModel
import com.cardiomonitor.app.domain.model.DeviceConnectionState

@Composable
fun DeviceScreen(viewModel: LiveMonitorViewModel) {
    val state by viewModel.connectionState.collectAsState()
    val data by viewModel.snapshot.collectAsState()
    val sync by viewModel.syncStatus.collectAsState()
    val connected = state is DeviceConnectionState.Connected
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(18.dp)) {
        SectionTitle("Устройство", "Подключение и состояние прибора")
        MedicalCard {
            Text("CardioMonitor", fontSize = 25.sp, fontWeight = FontWeight.SemiBold)
            Text("ESP32-C3 Super Mini", color = MedicalColors.Muted, fontSize = 13.sp)
            StatusPill(if (connected) "Соединение установлено" else "Прибор не подключён", connected)
            HorizontalDivider(color = MedicalColors.Border)
            InfoRow("Заряд аккумулятора", data?.battery?.let { "$it%" } ?: "Нет данных")
            InfoRow("Записи на microSD", if (data?.sd == true) data?.files?.toString() ?: "Подсчёт…" else "Нет карты / нет связи")
            InfoRow("Качество ЭКГ", if (data?.mode != 1) "ЭКГ не запущена" else if (data?.ecgOk == true) "Сигнал принят" else "Нет надёжного сигнала")
            InfoRow("Оптический датчик", if (data?.finger == true) "Палец обнаружен" else "Нет сигнала пальца")
            InfoRow("Запись на microSD", if (data?.recording == true) "Идёт" else "Остановлена")
        }
        MedicalCard {
            Text("Синхронизация", fontWeight = FontWeight.SemiBold)
            Text("Текущие показания и завершённые записи передаются автоматически в общей Wi-Fi-сети. Новая запись копируется после её остановки.",
                color = MedicalColors.Muted, fontSize = 13.sp, lineHeight = 20.sp)
            Text(sync, color = MedicalColors.Teal, fontSize = 13.sp)
            Button(onClick = viewModel::syncNow, enabled = connected, modifier = Modifier.fillMaxWidth().height(48.dp)) { Text("Синхронизировать записи") }
        }
        MedicalCard {
            Text("Об этом устройстве", fontWeight = FontWeight.SemiBold)
            Text("Учебный прототип для регистрации и изучения сигналов. Не является диагностическим медицинским прибором.",
                color = MedicalColors.Muted, fontSize = 13.sp, lineHeight = 20.sp)
            Text("Используйте доверенную Wi-Fi-сеть: локальный обмен пока без шифрования и сопряжения.", fontSize = 12.sp, color = MedicalColors.Muted)
            InfoRow("Оформление", "Светлое")
            InfoRow("Версия приложения", "0.1.0-beta")
        }
    }
}
