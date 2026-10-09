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

    init {
        repository.startListening(viewModelScope)
        viewModelScope.launch {
            repository.liveEcg.collect { sample ->
                _ecgWindow.value = (_ecgWindow.value + sample).takeLast(500)
            }
        }
    }
}
