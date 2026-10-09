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
    val connected = state is DeviceConnectionState.Connected
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(18.dp)) {
        SectionTitle("Устройство", "Подключение и состояние прибора")
        MedicalCard {
            Text("CardioMonitor", fontSize = 25.sp, fontWeight = FontWeight.SemiBold)
            Text("ESP32-C3 Super Mini", color = MedicalColors.Muted, fontSize = 13.sp)
            StatusPill(if (connected) "Соединение установлено" else "Прибор не подключён", connected)
            HorizontalDivider(color = MedicalColors.Border)
            InfoRow("Заряд аккумулятора", "Нет данных")
            InfoRow("Записи на microSD", "Нет данных")
            InfoRow("Контакт электродов", "Нет данных")
            InfoRow("Оптический датчик", "Нет данных")
        }
        MedicalCard {
            Text("Синхронизация", fontWeight = FontWeight.SemiBold)
            Text("Передача записей с microSD в приложение ещё не реализована. Подключение прибора к Wi-Fi само по себе не передаёт измерения.",
                color = MedicalColors.Muted, fontSize = 13.sp, lineHeight = 20.sp)
            Button(onClick = {}, enabled = false, modifier = Modifier.fillMaxWidth().height(48.dp)) { Text("Синхронизировать записи") }
        }
        MedicalCard {
            Text("Об этом устройстве", fontWeight = FontWeight.SemiBold)
            Text("Учебный прототип для регистрации и изучения сигналов. Не является диагностическим медицинским прибором.",
                color = MedicalColors.Muted, fontSize = 13.sp, lineHeight = 20.sp)
            InfoRow("Оформление", "Светлое")
            InfoRow("Версия приложения", "0.1.0-beta")
        }
    }
}
