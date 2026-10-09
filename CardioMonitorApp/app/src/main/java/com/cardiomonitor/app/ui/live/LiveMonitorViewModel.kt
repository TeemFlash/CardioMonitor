package com.cardiomonitor.app.ui.live

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.cardiomonitor.app.data.repository.AnalysisRepository
import com.cardiomonitor.app.domain.model.DeviceConnectionState
import com.cardiomonitor.app.domain.model.EcgSample
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch

class LiveMonitorViewModel(
    private val repository: AnalysisRepository
) : ViewModel() {

    val connectionState: StateFlow<DeviceConnectionState> = repository.connectionState

    // Скользящее окно последних ~500 точек ЭКГ для графика в реальном времени
    private val _ecgWindow = MutableStateFlow<List<EcgSample>>(emptyList())
    val ecgWindow: StateFlow<List<EcgSample>> = _ecgWindow

    val currentBpm = repository.livePulse
    val currentSpo2 = repository.liveSpo2

    val snapshot = repository.snapshot
    val syncStatus = repository.syncStatus
    fun syncNow() = repository.syncNow()

    init {
        viewModelScope.launch {
            repository.snapshot.collect { status ->
                _ecgWindow.value = if (status == null || status.mode != 1 || !status.ecgOk) emptyList()
                    else (_ecgWindow.value + status.ecg).takeLast(500)
            }
        }
    }
}
