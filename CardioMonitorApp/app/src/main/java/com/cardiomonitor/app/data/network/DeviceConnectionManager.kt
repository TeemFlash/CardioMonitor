package com.cardiomonitor.app.data.network

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.os.Build
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import com.cardiomonitor.app.domain.model.*
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.*
import org.json.JSONObject
import java.io.*
import java.net.InetSocketAddress
import java.net.Socket
import java.util.zip.CRC32
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

data class DeviceSnapshot(val mode: Int, val bpm: Int?, val spo2: Int?, val battery: Int?,
    val finger: Boolean, val ecgOk: Boolean, val sd: Boolean, val recording: Boolean,
    val files: Int?, val ecgBpm: Int?, val utcMs: Long?, val dropped: Long, val ecg: List<EcgSample>)

/** Local LAN protocol v1. No cloud, pairing or transport encryption. */
class DeviceConnectionManager(private val context: Context) {
    private data class Endpoint(val host: String, val port: Int)
    private val endpoint = MutableStateFlow<Endpoint?>(null)
    private val _state = MutableStateFlow<DeviceConnectionState>(DeviceConnectionState.Disconnected)
    val connectionState = _state.asStateFlow()
    val snapshot = MutableStateFlow<DeviceSnapshot?>(null)
    val syncStatus = MutableStateFlow("Поиск прибора в Wi-Fi…")
    private val syncRequests = Channel<Unit>(Channel.CONFLATED)
    private val prefs = context.getSharedPreferences("sd_import_v1", Context.MODE_PRIVATE)
    private var started = false
    private var multicast: WifiManager.MulticastLock? = null
    fun syncNow() { syncRequests.trySend(Unit) }

    @Suppress("DEPRECATION")
    fun start(scope: CoroutineScope, import: suspend (String, String, File) -> Unit) {
        if (started) return
        started = true
        val nsd = context.getSystemService(Context.NSD_SERVICE) as NsdManager
        if (Build.VERSION.SDK_INT <= 32) {
            val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            multicast = wifi.createMulticastLock("CardioMonitor discovery").apply { setReferenceCounted(false); acquire() }
        }
        val resolving = AtomicBoolean(false)
        val lastService = AtomicReference<NsdServiceInfo?>(null)
        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(type: String) {}
            override fun onDiscoveryStopped(type: String) {}
            override fun onStartDiscoveryFailed(type: String, code: Int) { syncStatus.value = "Ошибка поиска Wi-Fi: $code. Откройте приложение заново." }
            override fun onStopDiscoveryFailed(type: String, code: Int) {}
            override fun onServiceLost(info: NsdServiceInfo) { /* TCP timeout handles transient mDNS loss. */ }
            override fun onServiceFound(info: NsdServiceInfo) {
                if (!info.serviceType.contains("_cardiomonitor._tcp") || !resolving.compareAndSet(false, true)) return
                lastService.set(info)
                nsd.resolveService(info, object : NsdManager.ResolveListener {
                    override fun onResolveFailed(info: NsdServiceInfo, code: Int) { resolving.set(false) }
                    override fun onServiceResolved(info: NsdServiceInfo) {
                        resolving.set(false)
                        info.host?.hostAddress?.let { endpoint.value = Endpoint(it, info.port) }
                    }
                })
            }
        }
        nsd.discoverServices("_cardiomonitor._tcp.", NsdManager.PROTOCOL_DNS_SD, listener)
        scope.launch {
            while (isActive) {
                delay(10_000)
                if (_state.value !is DeviceConnectionState.Connected) lastService.get()?.let { listener.onServiceFound(it) }
            }
        }
        scope.launch(Dispatchers.IO) {
            endpoint.filterNotNull().collectLatest { address ->
                while (isActive) {
                    _state.value = DeviceConnectionState.Connecting
                    try {
                        connect(address).use { socket ->
                            val input = DataInputStream(BufferedInputStream(socket.getInputStream()))
                            val id = hello(input)
                            socket.getOutputStream().write("LIVE\n".toByteArray())
                            _state.value = DeviceConnectionState.Connected(id)
                            while (isActive) {
                                val frame = json(input)
                                require(frame.getString("type") == "live") { "Неверный поток" }
                                val points = frame.getJSONArray("ecg")
                                require(points.length() <= 80)
                                val ecg = (0 until points.length()).map { i ->
                                    val p = points.getJSONArray(i)
                                    val value = p.getDouble(1).toFloat()
                                    require(value.isFinite())
                                    EcgSample(p.getLong(0), value)
                                }
                                snapshot.value = DeviceSnapshot(frame.getInt("mode"),
                                    frame.getInt("hr").takeIf { it in 20..250 },
                                    frame.getInt("spo2").takeIf { it in 1..100 },
                                    frame.getInt("battery").takeIf { it in 0..100 },
                                    frame.getBoolean("finger"), frame.getBoolean("ecg_ok"),
                                    frame.getBoolean("sd"), frame.getBoolean("recording"),
                                    frame.getInt("files").takeIf { it >= 0 }, frame.optInt("ecg_hr", -1).takeIf { it in 20..250 },
                                    frame.optLong("utc_ms", 0).takeIf { it in 1704067200000L until 4102444800000L }, frame.getLong("dropped"), ecg)
                            }
                        }
                    } catch (e: CancellationException) { throw e }
                    catch (e: Exception) { _state.value = DeviceConnectionState.Error(e.message ?: "Нет связи") }
                    finally { snapshot.value = null }
                    delay(2000)
                }
            }
        }
        scope.launch(Dispatchers.IO) {
            while (isActive) {
                val address = endpoint.value
                if (address != null && _state.value is DeviceConnectionState.Connected) {
                    try { sync(address, import) }
                    catch (e: CancellationException) { throw e }
                    catch (e: Exception) { syncStatus.value = "Синхронизация: ${e.message ?: "нет связи"}. Повтор автоматически." }
                }
                withTimeoutOrNull(15_000) { syncRequests.receive() }
            }
        }
    }

    fun archiveFile(id: String): File? {
        val parts = id.split('/')
        if (parts.size != 2 || !parts[0].matches(Regex("cardio-[0-9a-f]{12}")) || !validArchiveName(parts[1])) return null
        return File(context.filesDir, "sd_archive/$id").takeIf { it.isFile }
    }

    private fun connect(address: Endpoint) = Socket().apply {
        try {
            soTimeout = 5000; tcpNoDelay = true
            val connectivity = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            connectivity.allNetworks.firstOrNull {
                connectivity.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true
            }?.bindSocket(this)
            connect(InetSocketAddress(address.host, address.port), 3000)
        }
        catch (e: Exception) { close(); throw e }
    }
    private fun hello(input: DataInputStream): String {
        val h = json(input)
        require(h.getString("type") == "hello" && h.getInt("version") == 1) { "Несовместимая прошивка" }
        return h.getString("device").also { require(it.matches(Regex("cardio-[0-9a-f]{12}"))) }
    }
    private fun json(input: DataInputStream): JSONObject {
        val line = readProtocolLine(input)
        return JSONObject(line).also {
            if (it.optString("type") == "error") {
                val message = when (val code = it.optString("message")) {
                    "no_sd" -> "Карта microSD недоступна"
                    "live_busy" -> "Прибор уже подключён к другому клиенту"
                    "file_busy_or_no_sd" -> "Запись ещё открыта или карта недоступна"
                    "not_found" -> "Файл больше не доступен"
                    else -> "Ошибка прибора: $code"
                }
                throw IOException(message)
            }
        }
    }
    private suspend fun sync(address: Endpoint, import: suspend (String, String, File) -> Unit) {
        syncStatus.value = "Проверка истории microSD…"
        val index = File.createTempFile("sd-index", ".jsonl", context.cacheDir)
        var copied = 0
        var failed = 0
        var lastFailure = ""
        try {
            // Persist the listing on disk: an arbitrarily long history never becomes one RAM array.
            val id = connect(address).use { socket ->
                val input = DataInputStream(BufferedInputStream(socket.getInputStream()))
                val device = hello(input)
                socket.getOutputStream().write("LIST\n".toByteArray())
                index.bufferedWriter().use { out ->
                    while (currentCoroutineContext().isActive) {
                        val entry = json(input)
                        if (entry.getString("type") == "list_end") break
                        require(entry.getString("type") == "entry")
                        require(validArchiveName(entry.getString("name")))
                        require(entry.getLong("size") >= 0)
                        out.write(entry.toString()); out.newLine()
                    }
                }
                device
            }
            index.bufferedReader().use { entries ->
                while (true) {
                    currentCoroutineContext().ensureActive()
                    val line = entries.readLine() ?: break
                    val entry = JSONObject(line)
                    val name = entry.getString("name")
                    val size = entry.getLong("size")
                    val key = "$id/$name"
                    if (prefs.getLong(key, -1) == size) continue
                    val directory = File(context.filesDir, "sd_archive/$id").apply { mkdirs() }
                    require(size < directory.usableSpace - 16L * 1024 * 1024) { "Недостаточно памяти телефона" }
                    syncStatus.value = "Копирование $name…"
                    val temp = File(directory, "$name.part")
                    try {
                        connect(address).use { socket ->
                            val input = DataInputStream(BufferedInputStream(socket.getInputStream()))
                            require(hello(input) == id)
                            socket.getOutputStream().write("GET $name\n".toByteArray())
                            val header = json(input)
                            require(header.getString("type") == "file" && header.getString("name") == name && header.getLong("size") == size)
                            val crc = CRC32()
                            temp.outputStream().use { out ->
                                val buffer = ByteArray(8192)
                                var remaining = size
                                while (remaining > 0) {
                                    currentCoroutineContext().ensureActive()
                                    val n = input.read(buffer, 0, minOf(buffer.size.toLong(), remaining).toInt())
                                    if (n < 0) throw EOFException("Передача прервана")
                                    out.write(buffer, 0, n); crc.update(buffer, 0, n); remaining -= n
                                }
                            }
                            val end = json(input)
                            require(end.getString("type") == "end" && end.getLong("crc32") == crc.value) { "Ошибка контрольной суммы" }
                        }
                        val file = File(directory, name)
                        require(temp.renameTo(file)) { "Не удалось сохранить CSV" }
                        import(id, name, file)
                        check(prefs.edit().putLong(key, size).commit()) { "Не удалось сохранить индекс" }
                        copied++
                    } catch (e: CancellationException) { throw e }
                    catch (e: Exception) { failed++; lastFailure = "$name: ${e.message}" }
                    finally { temp.delete() }
                }
            }
            syncStatus.value = if (failed > 0) "Скопировано: $copied. Ошибок: $failed. $lastFailure. Повтор автоматически." else if (copied > 0) "История обновлена: $copied записей" else "История синхронизирована"
        } finally { index.delete() }
    }
}

fun validArchiveName(name: String) = name.matches(Regex("(ecg|pulse|spo2)_[0-9]{1,9}\\.csv"))

/** Newline framed JSON followed, for GET, by an exact byte count. Never use BufferedReader on the socket. */
fun readProtocolLine(input: InputStream): String {
    val bytes = ByteArrayOutputStream()
    while (true) {
        val b = input.read()
        if (b < 0) throw EOFException("Соединение закрыто")
        if (b == 10) {
            if (bytes.size() == 0) continue // separator after the binary file payload
            return bytes.toString("UTF-8")
        }
        if (bytes.size() >= 8192) throw IOException("Слишком длинный пакет")
        bytes.write(b)
    }
}
