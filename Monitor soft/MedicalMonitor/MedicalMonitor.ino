// Cardio Monitor / ESP32-C3 Super Mini, ILI9341 320x240.
// Records live only on microSD. RAM holds bounded acquisition/log buffers.
// Experimental measurements, not a diagnostic or calibrated medical instrument.
// Build and hardware validation: ../README.md; ../tools/build.py.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "MAX30105.h"
#include <Preferences.h>
#include <WiFi.h>
#include <FS.h>
#include <SD.h>
#include <math.h>
#include "SignalProcessing.h"
#include "HistoryAnalysis.h"
#include "HistoryText.h"
#include "DisplayModel.h"

// ============================================================
// ВАЖНО ДЛЯ Arduino IDE: перечисления Language и StringID должны
// стоять здесь, ДО первой функции в файле (даже до маленькой rgb565()
// ниже). Arduino IDE автоматически генерирует прототипы для всех
// функций сразу после найденного места первой функции — и если эти
// типы объявлены позже (например, внутри секции "ЯЗЫКИ"), автогенератор
// вставит прототипы функций вроде T(StringID, Language) до того, как
// компилятор узнает о существовании этих типов, что ломает сборку.
// ============================================================
enum Language : uint8_t { LANG_EN = 0, LANG_RU = 1, LANG_KZ = 2, LANG_COUNT = 3 };

enum StringID {
  S_APP_TITLE, S_MENU_ECG, S_MENU_PULSE, S_MENU_SPO2, S_MENU_SETTINGS, S_NAV_HINT,
  S_ECG_TITLE, S_LEADS_OFF, S_BPM,
  S_PULSE_TITLE, S_SPO2_TITLE, S_PLACE_FINGER, S_HEART_RATE, S_SPO2,
  S_SETTINGS_TITLE, S_LANGUAGE, S_LANG_NAME_EN, S_LANG_NAME_RU, S_LANG_NAME_KZ,
  S_SAVED, S_BACK_HINT,
  S_WIFI_LABEL, S_WIFI_NOT_CONNECTED, S_WIFI_SCANNING, S_WIFI_RESCAN,
  S_WIFI_ENTER_PASSWORD, S_WIFI_CONNECTING, S_WIFI_CONNECTED, S_WIFI_FAILED,
  S_MENU_HISTORY, S_HISTORY_TITLE, S_HISTORY_ECG, S_HISTORY_PULSE, S_HISTORY_SPO2,
  S_HISTORY_EMPTY, S_HISTORY_NO_SD, S_HISTORY_RECORDS, S_HISTORY_AVERAGE,
  S_RECORDING, S_SCALE, S_DIAGNOSTICS, S_WIFI_POWER, S_FORGET_WIFI,
  S_ON, S_OFF, S_COLLECTING, S_POOR_SIGNAL, S_SENSOR_MISSING, S_READY, S_CONFIRM,
  S_OLDER, S_NEWER, S_READING,
  S_NOTCH, S_HR_AGREE, S_HR_DIFFER, S_HR_WAIT,
  S_AUTO_PPG, S_PPG_TUNING, S_PPG_WEAK, S_PPG_CLIPPED, S_PPG_UNSTABLE,
  S_COUNT
};

// ============================================================
// 1. КОНФИГУРАЦИЯ: ЭКРАН, ПИНЫ, ЦВЕТА, ПАРАМЕТРЫ
// ============================================================
#define SCREEN_W      320
#define SCREEN_H      240
#define HEADER_H       28

// Дисплей ILI9341 (SPI)
#define TFT_CS         7
#define TFT_DC         3
#define TFT_RST       -1   // RST аппаратный (припаян к 3.3V)
#define SPI_SCK        4
#define SPI_MOSI       6

// SD-карта (на той же шине SPI, что и дисплей — общие SCK/MOSI, у каждого
// устройства свой CS). У самого дисплея MISO (SDO) не подключён — он не
// нужен для записи, но встроенному картридеру он обязателен, поэтому
// SPI_MISO берём с пина SD_MISO картридера.
#define SPI_MISO               5   // SD_MISO картридера дисплея (переехал на GPIO 5)
#define SD_CS_PIN               0   // SD_CS картридера дисплея
// LED/BLK is wired to 3.3 V; brightness cannot be changed in software.

// Пульсоксиметр MAX30102 (I2C)
#define I2C_SDA        8
#define I2C_SCL        9

// Внешний АЦП ADS1115 (I2C, тот же адрес 0x48, ADDR -> GND).
// Через него подключены: ЭКГ (A0), батарея (A1), кнопка "Назад" (A2).
#define ADS1115_ADDR       0x48
#define ADS_CH_ECG            0   // AD8232 OUTPUT -> ADS1115 A0
#define ADS_CH_BATTERY         1   // делитель напряжения батареи -> ADS1115 A1
#define ADS_CH_BTN_BACK        2   // TTP223 №4 "Назад" -> ADS1115 A2

// Сенсорные кнопки TTP223: "Вверх", "Вниз" и "OK" — на прямых GPIO
// (OK — GPIO10, см. ниже), "Назад" читается через ADS1115 (см.
// ADS_CH_BTN_BACK выше, acquisitionTask()).
#define BTN_UP         1
#define BTN_DOWN       2
#define BTN_OK        10

// Батарея (делитель напряжения читается через ADS1115, канал ADS_CH_BATTERY)
#define BATTERY_DIVIDER_RATIO 2.0f  // 100k / 100k; calibrate with a multimeter.
#define PPG_GAIN 0.015f             // Display scaling only.

// Wi-Fi
#define MAX_WIFI_NETS           15
#define MAX_WIFI_PASS_LEN       63
#define WIFI_VISIBLE_ROWS        4
#define WIFI_CONNECT_TIMEOUT_MS 15000

// Идентификаторы экранов
enum ScreenID { SCR_MAIN = 0, SCR_ECG = 1, SCR_PULSE = 2, SCR_SPO2 = 3, SCR_SETTINGS = 4, SCR_WIFI = 5,
                SCR_HISTORY = 6, SCR_HISTORY_LIST = 7, SCR_HISTORY_DETAIL = 8, SCR_DIAGNOSTICS = 9, SCR_FORGET = 10 };
#define MAIN_MENU_COUNT 5

// Экранная клавиатура для ввода пароля (перебор стрелками)
const char KB_CHARS[] = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+.,:;/\\\"\'[]{}<>?|~`";
const int KB_LEN = sizeof(KB_CHARS) - 1;
const int KB_SPECIAL_BACKSPACE = KB_LEN;
const int KB_SPECIAL_ENTER     = KB_LEN + 1;
const int KB_TOTAL             = KB_LEN + 2;

// Both fonts include Russian and the Kazakh letters used by this UI.
#define FONT_SMALL   u8g2_font_6x12_t_cyrillic
#define FONT_MED     u8g2_font_10x20_t_cyrillic

// ---------------- Цветовая палитра (RGB565) ----------------
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

const uint16_t COL_BG         = rgb565(255, 255, 255);
const uint16_t COL_BG_LIGHT   = rgb565(255, 255, 255);
const uint16_t COL_CARD       = rgb565(255, 255, 255);
const uint16_t COL_CARD_SEL   = rgb565(220, 242, 239);
const uint16_t COL_ACCENT     = rgb565(0, 119, 112);
const uint16_t COL_ACCENT_DIM = rgb565(163, 207, 201);
const uint16_t COL_TEXT       = rgb565(20, 32, 43);
const uint16_t COL_TEXT_DIM   = rgb565(91, 107, 120);
const uint16_t COL_GOOD       = rgb565(0, 119, 112);
const uint16_t COL_WARN       = rgb565(158, 95, 12);
const uint16_t COL_BAD        = rgb565(183, 51, 67);
const uint16_t COL_ECG_LINE   = rgb565(20, 32, 43);
const uint16_t COL_PULSE_LINE = rgb565(192, 57, 43);
const uint16_t COL_SWEEP      = rgb565(163, 207, 201);
const uint16_t COL_GRID_V     = rgb565(218, 229, 232);
const uint16_t COL_GRID_H     = rgb565(218, 229, 232);
const uint16_t COL_OXYGEN     = rgb565(26, 90, 153);
const uint16_t COL_ECG_ACCENT = rgb565(151, 111, 12);
const uint16_t COL_PAPER      = rgb565(255, 245, 246);
const uint16_t COL_PAPER_FINE = rgb565(242, 216, 222);
const uint16_t COL_PAPER_BOLD = rgb565(228, 175, 185);
const uint16_t COL_WARNING_BG = rgb565(255, 241, 194);
const uint16_t COL_TRACK      = rgb565(220, 229, 232);

// ============================================================
// 2. ЯЗЫКИ (перечисления Language/StringID объявлены в самом верху
// файла, сразу после #include — так требует автогенератор
// прототипов функций в Arduino IDE, см. комментарий там)
// ============================================================
static const char* const STR_EN[S_COUNT] = {
  "MEDICAL MONITOR", "ECG Graph", "Pulse", "SpO2", "Settings", "UP  DOWN  OK  BACK",
  "ECG MONITOR", "CHECK ECG SIGNAL", "BPM",
  "PULSE MONITOR", "OXYGEN MONITOR", "PLACE FINGER ON SENSOR", "Heart Rate", "Blood Oxygen",
  "SETTINGS", "Language", "English", "Russian", "Kazakh",
  "Saved", "BACK: return to menu",
  "Wi-Fi", "Not connected", "Scanning networks...", "Rescan",
  "Enter password", "Connecting...", "Connected", "Connection failed",
  "History", "HISTORY", "ECG history", "Pulse history", "SpO2 history",
  "No saved records", "No SD card", "Records", "Average",
  "SD recording", "ECG gain", "Device status", "Wi-Fi radio", "Forget network", "On", "Off", "Collecting signal...", "Check signal / keep still", "Sensor unavailable", "Signal accepted", "OK: confirm / BACK: cancel",
  "Older records", "Newer records", "Reading SD...",
  "Mains filter", "HR estimates agree", "HR differs: check sensors", "ECG + finger: compare HR",
  "MAX30102 auto LED", "Adjusting optical signal...", "Weak signal: adjust finger", "Signal clipped: adjust finger", "Unstable signal: keep still"
};

static const char* const STR_RU[S_COUNT] = {
  "МЕДИЦИНСКИЙ МОНИТОР", "График ЭКГ", "Пульс", "SpO2", "Настройки", "ВВЕРХ  ВНИЗ  OK  НАЗАД",
  "МОНИТОР ЭКГ", "ПРОВЕРЬТЕ СИГНАЛ ЭКГ", "УД/МИН",
  "МОНИТОР ПУЛЬСА", "МОНИТОР КИСЛОРОДА", "ПРИЛОЖИТЕ ПАЛЕЦ К ДАТЧИКУ", "Частота пульса", "Кислород в крови",
  "НАСТРОЙКИ", "Язык", "Английский", "Русский", "Казахский",
  "Сохранено", "НАЗАД: выход в меню",
  "Wi-Fi", "Не подключено", "Поиск сетей...", "Обновить",
  "Введите пароль", "Подключение...", "Подключено", "Не удалось подключиться",
  "История", "ИСТОРИЯ", "История ЭКГ", "История пульса", "История SpO2",
  "Нет сохранённых записей", "Нет SD-карты", "Записей", "Среднее",
  "Запись на SD", "Масштаб ЭКГ", "Состояние устройства", "Радио Wi-Fi", "Забыть сеть", "Вкл", "Выкл", "Сбор сигнала...", "Проверьте сигнал / не двигайтесь", "Датчик недоступен", "Сигнал принят", "OK: подтвердить / НАЗАД: отмена",
  "Более ранние", "Более новые", "Чтение SD...",
  "Фильтр сети", "ЧСС датчиков согласуется", "ЧСС отличается: проверьте сигнал", "ЭКГ + палец: сравнение ЧСС",
  "Автонастройка MAX30102", "Настройка оптического сигнала...", "Слабый сигнал: поправьте палец", "Перегрузка: поправьте палец", "Сигнал нестабилен: не двигайтесь"
};

static const char* const STR_KZ[S_COUNT] = {
  "МЕДИЦИНАЛЫҚ МОНИТОР", "ЭКГ графигі", "Пульс", "SpO2", "Баптаулар", "ЖОҒАРЫ  ТӨМЕН  OK  АРТҚА",
  "ЭКГ МОНИТОРЫ", "ЭКГ СИГНАЛЫН ТЕКСЕРІҢІЗ", "СОҒУ/МИН",
  "ПУЛЬС МОНИТОРЫ", "ОТТЕГІ МОНИТОРЫ", "САУСАҒЫҢЫЗДЫ ҚОЙЫҢЫЗ", "Жүрек соғу жиілігі", "Қандағы оттегі",
  "БАПТАУЛАР", "Тіл", "Ағылшын", "Орыс", "Қазақ",
  "Сақталды", "АРТҚА: мәзірге шығу",
  "Wi-Fi", "Қосылмаған", "Желілерді іздеу...", "Жаңарту",
  "Құпия сөзді енгізіңіз", "Қосылуда...", "Қосылды", "Қосылу сәтсіз аяқталды",
  "Тарих", "ТАРИХ", "ЭКГ тарихы", "Пульс тарихы", "SpO2 тарихы",
  "Сақталған жазба жоқ", "SD-карта жоқ", "Жазбалар", "Орташа",
  "SD жазбасы", "ЭКГ масштабы", "Құрылғы күйі", "Wi-Fi радиосы", "Желіні ұмыту", "Қосулы", "Өшірулі", "Сигнал жиналуда...", "Сигналды тексеріңіз", "Сенсор қолжетімсіз", "Сигнал қабылданды", "OK: растау / АРТҚА: бас тарту",
  "Ескі жазбалар", "Жаңа жазбалар", "SD оқылуда...",
  "Желі сүзгісі", "ЖСЖ сәйкес келеді", "ЖСЖ бөлек: сигналды тексеріңіз", "ЭКГ + саусақ: ЖСЖ салыстыру",
  "MAX30102 автобаптау", "Оптикалық сигнал бапталуда...", "Әлсіз сигнал: саусақты түзетіңіз", "Сигнал артық: саусақты түзетіңіз", "Сигнал тұрақсыз: қозғалмаңыз"
};

static const char* const* const LANG_TABLE[LANG_COUNT] = { STR_EN, STR_RU, STR_KZ };

inline const char* T(StringID id, Language lang) {
  return LANG_TABLE[lang][id];
}

// ============================================================
// 3. ГЛОБАЛЬНЫЕ ОБЪЕКТЫ
// ============================================================
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;
MAX30105 pulseSensor;

Preferences prefs;
#include "Acquisition.h"

// ============================================================
// 4. ГЛОБАЛЬНОЕ СОСТОЯНИЕ
// ============================================================
Language currentLanguage = LANG_RU;
int currentScreen = SCR_MAIN;
int selectedItem = 0;

// Настройки
int settingsSelected = 0;
const int SETTINGS_COUNT = 3; // Wi-Fi, Bluetooth status, language
bool fineGrid = true;
bool recordingEnabled = true, wifiEnabled = true;
uint8_t ecgGain = 1;
uint8_t ecgMains = 50;
EcgPlotBuffer ecgPlot;
uint32_t previousEcgBin=0;
bool ecgPlotConnected=false;
bool sdError = false;
uint32_t sessionCommand = 0, sessionStart = 0;
uint16_t activePpgEpoch=0;
bool havePpgEpoch=false;
uint32_t lastUi = 0, lastHeader = 0;
uint32_t displayRevision=1;
int displayedScreen=-1;
Language displayedLanguage=LANG_RU;
// 10,368 bytes once, not a full-screen framebuffer (153,600 bytes).
GFXcanvas16 numberCanvas(108,48);
void clearDisplay() {
  tft.fillScreen(COL_BG);
  ++displayRevision;displayedScreen=currentScreen;displayedLanguage=currentLanguage;
}
void drawLargeReading(int value,uint16_t color,int x,int y) {
  char text[5];if(value<0)strcpy(text,"--");else snprintf(text,sizeof(text),"%d",value);
  if(numberCanvas.getBuffer()) {
    numberCanvas.fillScreen(COL_BG);numberCanvas.setTextWrap(false);
    numberCanvas.setTextSize(6);numberCanvas.setTextColor(color,COL_BG);
    numberCanvas.setCursor((108-int(strlen(text))*36)/2,0);numberCanvas.print(text);
    tft.drawRGBBitmap(x,y,numberCanvas.getBuffer(),108,48);
  } else {
    tft.setTextSize(6);tft.setTextColor(color,COL_BG);tft.setCursor(x,y);
    tft.print(text);for(size_t i=strlen(text);i<3;++i)tft.print(" ");
  }
}
DisplayStatistics displayPulseStats;
uint32_t lastDisplayStats=0;
int latestEcg = 2048;
uint32_t latestIr = 0;
float graphLow = 0, graphHigh = 0;
bool graphHasSample = false;
bool ppgPlotConnected = false;
DebouncedButton buttons[4];
char logBuffer[4096];
size_t logUsed = 0;
uint32_t lastLogFlush = 0;
File historyReader;
bool historyLoading = false;
cardio::HistoryAnalysis historyAnalysis;
cardio::HistoryCsvStream historyCsv(historyAnalysis);
size_t historyExpectedBytes = 0, historyBytesRead = 0;
uint8_t historyTab = 0; // Summary, Analysis, Help; OK cycles tabs.
bool historyContextPending = false, historyAdultRest = false;
bool historyContextChosen = false;
uint8_t historyContextChoice = 1; // Never assume an adult resting recording.
unsigned historyTextScroll = 0, historyTextLines = 0, historyTextCursor = 0;
constexpr unsigned HISTORY_TEXT_ROWS = 11;

// ЭКГ
const int GRAPH_TOP    = 78;
const int GRAPH_BOTTOM = 170;
int graphX = 0;
int prevEcgY = (GRAPH_TOP + GRAPH_BOTTOM) / 2;

// Пульс (график PPG)
const int PPG_TOP    = 142;
const int PPG_BOTTOM = 192;
float ppgBaseline = 0;
bool ppgBaselineInit = false;

// Wi-Fi
enum WifiUiState { WIFI_UI_SCANNING, WIFI_UI_LIST, WIFI_UI_PASSWORD, WIFI_UI_CONNECTING, WIFI_UI_RESULT };
WifiUiState wifiState = WIFI_UI_LIST;
String wifiSSID[MAX_WIFI_NETS];
int wifiRSSI[MAX_WIFI_NETS];
int wifiEnc[MAX_WIFI_NETS];
int wifiNetCount = 0;
int wifiListSelected = 0;
int wifiListScroll = 0;
int selectedNetworkIndex = -1;
char wifiPassBuf[MAX_WIFI_PASS_LEN + 1] = {0};
int wifiPassLen = 0;
int kbIndex = 0;
unsigned long connectStartMs = 0;
bool wifiConnectOk = false;

// SD-карта (логирование измерений)
bool sdAvailable = false;
uint32_t sdNextSequence = 1;
File logFile;
bool logFileOpen = false;

// История измерений (просмотр ранее сохранённых CSV-логов с SD-карты)
int historyMenuSelected = 0;
const int HISTORY_MENU_COUNT = 3; // ЭКГ, Пульс, SpO2
int historyCategory = 0;          // 0=ecg, 1=pulse, 2=spo2
const char* const HISTORY_PREFIXES[3] = {"ecg", "pulse", "spo2"};
#define HISTORY_MAX_FILES     12
#define HISTORY_VISIBLE_ROWS   4
String historyFiles[HISTORY_MAX_FILES];
uint32_t historyIds[HISTORY_MAX_FILES] = {};
File historyDirectory;
bool historyScanning=false, historyHasOlder=false, historyHasNewer=false;
int historyDirection=0;
uint32_t historyBoundary=0, historyCandidates=0;
int historyFileCount = 0;
int historyListSelected = 0;
int historyListScroll = 0;
String historyDetailFile;

// ============================================================
// 5. МЕЛКИЕ УТИЛИТЫ
// ============================================================
void kbLabelFor(int idx, char* out) {
  if (idx == KB_SPECIAL_BACKSPACE) { strcpy(out, "DEL"); }
  else if (idx == KB_SPECIAL_ENTER) { strcpy(out, "OK"); }
  else if (KB_CHARS[idx] == ' ') { strcpy(out, "_"); }
  else { out[0] = KB_CHARS[idx]; out[1] = '\0'; }
}

// ============================================================
// 6. ИКОНКИ
// ============================================================










// ============================================================
// 7. ОБРАБОТКА СИГНАЛОВ (ЭКГ / MAX30102)
// ============================================================

// Сигнал AD8232 приходит через канал A0 внешнего АЦП ADS1115 (16 бит,
// однополярный отсчёт 0..~26400 при GAIN_ONE/±4.096В и питании 3.3V).
// Приводим его к диапазону 0..4095, чтобы не переписывать пороги
// ECG_BASELINE/ECG_LEADS_OFF_*, откалиброванные под старый 12-битный АЦП.
EcgBeatDetector ecgDetector;
PulseOxProcessor poxProcessor;

// ============================================================
// 8. БАТАРЕЯ, ШАПКА, ОБЩИЕ ВИДЖЕТЫ
// ============================================================
float readBatteryVoltage() { return sensorHealth.ads ? sensorHealth.battery : 0; }

int batteryPercent() { return estimateBatteryPercent(readBatteryVoltage()); }





void updateHeaderStatus() {
  if(millis()-lastHeader<1000)return;
  lastHeader=millis();
  int pct=batteryPercent();char charge[8];
  if(pct<0)strcpy(charge,"--%");else snprintf(charge,sizeof(charge),"%d%%",pct);
  char text[32];snprintf(text,sizeof(text),"SD%s W%s %s",sdError?"!":sdAvailable?"+":"-",
    WiFi.status()==WL_CONNECTED?"+":"-",charge);
  static DisplayTextCache cache;if(!cache.update(text,displayRevision))return;
  tft.setTextSize(1);tft.setTextColor(COL_TEXT_DIM,COL_BG_LIGHT);tft.setCursor(212,11);
  tft.print(text);for(size_t i=strlen(text);i<18;++i)tft.print(" ");
}

void drawHeader(StringID titleId) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COL_BG_LIGHT);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, COL_TRACK);
  u8g2.setFont(FONT_SMALL); u8g2.setFontMode(1);
  u8g2.setForegroundColor(COL_TEXT); u8g2.setBackgroundColor(COL_BG_LIGHT);
  printFit(titleId==S_APP_TITLE ? "CARDIO / MONITOR" : T(titleId, currentLanguage), 8, 18, 194);
  lastHeader = millis() - 1000; updateHeaderStatus();
}

// Значок активности/записи — рисуется В ТЕЛЕ экрана (а не в шапке, где
// уже занято место под батарею/Wi-Fi/SD), в безопасном месте без
// пересечения с графиком. Зелёная точка мигает, пока экран "живой"
// (идёт измерение), красная "REC" появляется, когда файл на SD открыт.
void drawRecBadge(int x,int y,uint16_t bg,bool logging) {
  const char* text=sdError?"SD!":logging?"REC":"";
  static DisplayTextCache cache;if(!cache.update(text,displayRevision))return;
  tft.setTextSize(1);tft.setTextColor(sdError?COL_BAD:COL_PULSE_LINE,bg);
  tft.setCursor(x,y+4);tft.print(text);for(size_t i=strlen(text);i<7;++i)tft.print(" ");
}

// ---------------- Логирование измерений на SD-карту ----------------
String nextLogFilename(const char* prefix) {
  // Reconstructed from SD filenames at mount; no log counter in NVS.
  char name[40];
  do {
    if(sdNextSequence>999999999) return "";
    snprintf(name,sizeof(name),"/%s_%09lu.csv",prefix,(unsigned long)sdNextSequence++);
  } while(SD.exists(name));
  return String(name);
}

void closeLogFile() {
  flushLogBuffer();
  if (logFileOpen) { logFile.flush(); logFile.close(); logFileOpen = false; }
  logUsed = 0;
}

void openLogFile(const char* prefix, const char* header) {
  closeLogFile();
  if (!sdAvailable || !recordingEnabled) return;
  String name = nextLogFilename(prefix);
  if (name.isEmpty()) { sdError = true; return; }
  logFile = SD.open(name.c_str(), FILE_WRITE);
  if (!logFile || logFile.println(header) == 0) {
    sdError = true; sdAvailable = false; if (logFile) logFile.close(); return;
  }
  logFileOpen = true; lastLogFlush = millis();
  char metadata[80];
  snprintf(metadata,sizeof(metadata),"# ppg_auto=%u,default_led=60,led_limit=100\n",ppgAutoEnabled.load()?1:0);
  appendLog(metadata);
}

// ============================================================
// 9. ГЛАВНОЕ МЕНЮ
// ============================================================
// Stable navigation: UP/DOWN select, OK opens, BACK returns one level.
bool isMeasurementScreen() {
  return currentScreen==SCR_ECG || currentScreen==SCR_PULSE || currentScreen==SCR_SPO2;
}
const char* UI(const char* en,const char* ru,const char* kz) {
  return currentLanguage==LANG_RU?ru:(currentLanguage==LANG_KZ?kz:en);
}
int ecgGraphTop() { return currentScreen==SCR_PULSE?PPG_TOP:GRAPH_TOP; }
int ecgGraphBottom() { return currentScreen==SCR_PULSE?PPG_BOTTOM:GRAPH_BOTTOM; }
void drawMonitorNavigation() {}
void drawMeasurementTabs() {
  const char* title=currentScreen==SCR_ECG?UI("ECG","ЭКГ","ЭКГ"):
    currentScreen==SCR_PULSE?UI("PULSE","ПУЛЬС","ПУЛЬС"):"SpO2";
  uint16_t color=currentScreen==SCR_ECG?COL_ECG_ACCENT:currentScreen==SCR_PULSE?COL_PULSE_LINE:COL_OXYGEN;
  tft.fillRect(0,28,320,24,COL_BG);
  tft.fillRect(8,34,3,12,color);
  u8g2.setFont(FONT_SMALL);u8g2.setForegroundColor(color);
  u8g2.setCursor(18,43);u8g2.print(title);
  tft.drawFastHLine(0,51,320,COL_TRACK);
  drawMonitorNavigation();
}
void drawMeasurementFrame() {
  clearDisplay(); drawHeader(S_APP_TITLE); drawMeasurementTabs();
}

void drawOpticalNumber(bool oxygen) {
  int value=oxygen?(poxProcessor.spo2Valid?int(roundf(poxProcessor.spo2)):-1):
    (poxProcessor.hrValid?poxProcessor.beatAvg:-1);
  char key[40];snprintf(key,sizeof(key),"%d/%d/%d",value,poxProcessor.finger,oxygen);
  static DisplayTextCache cache;if(!cache.update(key,displayRevision))return;
  drawLargeReading(value,oxygen?COL_OXYGEN:COL_PULSE_LINE,106,101);
  u8g2.setFont(FONT_SMALL);u8g2.setFontMode(0);u8g2.setBackgroundColor(COL_BG);
  u8g2.setForegroundColor(COL_TEXT_DIM);
  const char* unit=oxygen?UI("% oxygen","% кислорода","% оттегі"):T(S_BPM,currentLanguage);
  u8g2.setCursor((SCREEN_W-u8g2.getUTF8Width(unit))/2,170);u8g2.print(unit);
  if(oxygen) {
    u8g2.setCursor(12,209);u8g2.print(poxProcessor.finger?
      UI("Finger detected   ","Палец на месте    ","Саусақ бар        "):
      UI("Place finger      ","Приложите палец   ","Саусақты қойыңыз  "));
  }
  u8g2.setFontMode(1);
}

void updateDisplayStatistics() {
  if(millis()-lastDisplayStats<1000)return;
  lastDisplayStats=millis();
  displayPulseStats.add(poxProcessor.beatAvg,poxProcessor.hrValid);
}
void drawMonitorCaption(const char* text) {
  drawRecBadge(266,197,COL_BG,logFileOpen);
  static DisplayTextCache cache;if(!cache.update(text,displayRevision))return;
  tft.fillRect(8,195,248,17,COL_BG);
  u8g2.setFont(FONT_SMALL);u8g2.setForegroundColor(COL_TEXT_DIM);printFit(text,10,208,246);

}

void drawMainMenu() {
  currentScreen=SCR_MAIN;
  bool full=displayedScreen!=currentScreen || displayedLanguage!=currentLanguage;
  static int previous=-1;
  if(full){clearDisplay();drawHeader(S_APP_TITLE);previous=-1;}

  int yPos[MAIN_MENU_COUNT] = {34, 70, 106, 142, 178};
  int cardH = 32;
  StringID labels[MAIN_MENU_COUNT] = {S_MENU_ECG, S_MENU_PULSE, S_MENU_SPO2, S_MENU_HISTORY, S_MENU_SETTINGS};

  for (int i = 0; i < MAIN_MENU_COUNT; i++) {
    if(previous>=0 && i!=previous && i!=selectedItem)continue;
    bool sel = (selectedItem == i);
    uint16_t bg = sel ? COL_CARD_SEL : COL_CARD;
    uint16_t border = sel ? COL_ACCENT : COL_TRACK;

    tft.fillRoundRect(14, yPos[i], SCREEN_W - 28, cardH, 9, bg);
    tft.drawRoundRect(14, yPos[i], SCREEN_W - 28, cardH, 9, border);

    u8g2.setFont(FONT_MED);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(sel ? COL_TEXT : COL_TEXT_DIM);
    u8g2.setBackgroundColor(bg);
    printFit(T(labels[i], currentLanguage),26,yPos[i] + cardH / 2 + 7,268);


  }

  previous=selectedItem;
}

// ============================================================
// 10. ЭКРАН ЭКГ
// ============================================================


void enterEcgScreen() {
  beginMeasurement(1);
  drawMeasurementFrame();
  graphX = 0; prevEcgY = (GRAPH_TOP + GRAPH_BOTTOM)/2;
  drawGraphGrid(GRAPH_TOP, GRAPH_BOTTOM);
  openLogFile("ecg", "millis_ms,kind,adc_code,filtered_relative,signal_ok,ir,red,ppg_valid,spo2_valid,correlation,ppg_bpm,spo2,ecg_bpm");
  if(logFileOpen) {
    char metadata[96];
    snprintf(metadata,sizeof(metadata),"# filters=HP0.5_LP40,notch=%u,nominal_ecg_hz=250,ppg_hz=25\n",ecgMains);
    appendLog(metadata);
  }
  sessionStart=millis(); ecgPlot.reset(sessionStart); ecgPlotConnected=false;
  sensorCommand.store(sessionCommand);
}

void renderECG() {
  renderEcgWave();
  static uint32_t last=0;if(millis()-last<200)return;last=millis();
  static DisplayTextCache rateCache;char rateKey[24];
  int currentRate=ecgDetector.quality && ecgDetector.bpm>0?int(roundf(ecgDetector.bpm)):-1;
  snprintf(rateKey,sizeof(rateKey),"%d",currentRate);
  if(rateCache.update(rateKey,displayRevision)) {
  u8g2.setFontMode(0);u8g2.setBackgroundColor(COL_BG);
  u8g2.setFont(FONT_SMALL);u8g2.setForegroundColor(COL_TEXT_DIM);
  u8g2.setCursor(8,69);u8g2.print(UI("Single channel / rel.","Один канал / отн. ед.","Бір арна / салыст."));
  char rate[24];if(ecgDetector.quality && ecgDetector.bpm>0)snprintf(rate,sizeof(rate),"%-3d BPM",int(roundf(ecgDetector.bpm)));else strcpy(rate,"--  BPM");
  u8g2.setForegroundColor(COL_PULSE_LINE);u8g2.setCursor(251,69);u8g2.print(rate);u8g2.setFontMode(1);
  }
  StringID state=(!sensorHealth.ads || !acquisitionStarted)?S_SENSOR_MISSING:
    (ecgDetector.initialized && millis()-ecgDetector.start<3000?S_COLLECTING:(ecgDetector.quality?S_READY:S_LEADS_OFF));
  drawMeasurementStatus(state,186);
  char caption[80];snprintf(caption,sizeof(caption),"0.2 s/div  x%u  0.5-40 Hz  %uHz",1<<ecgGain,ecgMains);
  drawMonitorCaption(caption);
}

// ============================================================
// 11. ЭКРАН ПУЛЬСА (график + анимация удара)
// ============================================================


void enterPulseScreen() {
  beginMeasurement(2);
  drawMeasurementFrame();

  openLogFile("pulse", "millis_ms,ir,red,hr_valid,spo2_valid,correlation,spo2,bpm");
  sessionStart=millis(); ecgPlot.reset(sessionStart);ecgPlotConnected=false; sensorCommand.store(sessionCommand);
}

void renderPulseOnly() {
  updateDisplayStatistics();
  static uint32_t last=0;if(millis()-last<100)return;last=millis();
  drawOpticalNumber(false);drawMeasurementStatus(pulseState(),234);
  drawRecBadge(266,30,COL_BG,logFileOpen);
}

// ============================================================
// 12. ЭКРАН SpO2
// ============================================================
void enterSpo2Screen() {
  beginMeasurement(2);
  drawMeasurementFrame();

  openLogFile("spo2", "millis_ms,ir,red,hr_valid,spo2_valid,correlation,bpm,spo2");
  sessionStart=millis(); ecgPlot.reset(sessionStart);ecgPlotConnected=false; sensorCommand.store(sessionCommand);
}

void renderSpo2Only() {
  graphHasSample=false;
  static uint32_t last=0;if(millis()-last<100)return;last=millis();
  drawOpticalNumber(true);drawMeasurementStatus(pulseState(),234);
  drawRecBadge(266,30,COL_BG,logFileOpen);
}

// ============================================================
// 13. НАСТРОЙКИ
// ============================================================

void drawSettingsMenu() {
  static DisplayTextCache rowCache[SETTINGS_COUNT];
  if(displayedScreen!=currentScreen || displayedLanguage!=currentLanguage) {
    clearDisplay();drawHeader(S_SETTINGS_TITLE);
  }
  const char* labels[]={"Wi-Fi","Bluetooth",T(S_LANGUAGE,currentLanguage)};
  const StringID langNames[]={S_LANG_NAME_EN,S_LANG_NAME_RU,S_LANG_NAME_KZ};
  const String values[]={
    WiFi.status()==WL_CONNECTED?WiFi.SSID():String(T(S_WIFI_NOT_CONNECTED,currentLanguage)),
    String(UI("Not implemented","Не реализован","Іске асырылмаған")),
    String(T(langNames[currentLanguage],currentLanguage))
  };
  for(int i=0;i<SETTINGS_COUNT;++i) {
    String key=String(i==settingsSelected)+"/"+values[i];
    if(!rowCache[i].update(key.c_str(),displayRevision))continue;
    int y=42+i*52;uint16_t bg=i==settingsSelected?COL_CARD_SEL:COL_CARD;
    tft.fillRect(12,y,296,46,bg);
    u8g2.setFont(FONT_SMALL);u8g2.setBackgroundColor(bg);
    u8g2.setForegroundColor(COL_TEXT);printFit(labels[i],24,y+17,270);
    u8g2.setForegroundColor(COL_TEXT_DIM);printFit(values[i],24,y+35,270);
  }
}

void cycleLanguage() {
  currentLanguage = Language((currentLanguage + 1) % LANG_COUNT);
  prefs.putUChar("lang", uint8_t(currentLanguage)); drawSettingsMenu();
}

// ============================================================
// 13.1. ИСТОРИЯ ИЗМЕРЕНИЙ (просмотр CSV-логов с SD-карты)
// ============================================================

// Ищет на SD-карте файлы вида "<prefix>_XXXX.csv" для выбранной категории
// (0=ecg, 1=pulse, 2=spo2) и складывает их имена в historyFiles[], отсортировав
// по убыванию номера (самые новые записи — сверху списка).
void scanHistoryFiles(int category) {
  historyCategory=category; historyBoundary=0; historyDirection=0;
  startHistoryScan();
}

// Open a CSV stream; serviceHistory() computes statistics incrementally.
void computeHistoryDetail(const String &filename) {
  if (historyReader) historyReader.close();
  historyDetailFile = filename;
  historyCsv.reset(); historyExpectedBytes=historyBytesRead=0;
  historyTab = 0; historyTextScroll = 0;
  historyContextPending = historyContextChosen = historyAdultRest = false;
  historyContextChoice = 1;
  historyReader = SD.open(filename.c_str(), FILE_READ);
  historyLoading = bool(historyReader);
  if(historyLoading) historyExpectedBytes=historyReader.size();
  if (!historyLoading) { historyAnalysis.markIoError(); historyAnalysis.finish(); }
}

void enterHistoryScreen() {
  historyMenuSelected = 0;
  drawHistoryMenu();
}

void drawHistoryMenu() {
  bool full=displayedScreen!=currentScreen || displayedLanguage!=currentLanguage;
  static int previous=-1;
  if(full){clearDisplay();drawHeader(S_HISTORY_TITLE);previous=-1;}

  int yPos[HISTORY_MENU_COUNT] = {42, 98, 154};
  int cardH = 42;
  StringID labels[HISTORY_MENU_COUNT] = {S_HISTORY_ECG, S_HISTORY_PULSE, S_HISTORY_SPO2};

  for (int i = 0; i < HISTORY_MENU_COUNT; i++) {
    if(previous>=0 && i!=previous && i!=historyMenuSelected)continue;
    bool sel = (historyMenuSelected == i);
    uint16_t bg = sel ? COL_CARD_SEL : COL_CARD;
    uint16_t border = sel ? COL_ACCENT : COL_TRACK;

    tft.fillRoundRect(14, yPos[i], SCREEN_W - 28, cardH,0, bg);
    tft.drawRoundRect(14, yPos[i], SCREEN_W - 28, cardH, 9, border);

    u8g2.setFont(FONT_MED);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(sel ? COL_TEXT : COL_TEXT_DIM);
    u8g2.setBackgroundColor(bg);
    printFit(T(labels[i], currentLanguage),26,yPos[i] + cardH / 2 + 7,268);


  }

  previous=historyMenuSelected;
}

void drawHistoryList() {
  clearDisplay(); drawHeader(S_HISTORY_TITLE);
  u8g2.setFont(FONT_SMALL); u8g2.setForegroundColor(COL_TEXT_DIM);
  if(historyScanning || !historyFileCount) {
    StringID msg=historyScanning?S_READING:(sdAvailable?S_HISTORY_EMPTY:S_HISTORY_NO_SD);
    printFit(T(msg,currentLanguage),20,120,280);
  } else {
    for(int row=0;row<4;++row) {
      int i=historyListScroll+row;
      if(i>=historyItemCount()) break;
      int y=42+row*42;
      uint16_t bg=i==historyListSelected?COL_CARD_SEL:COL_CARD;
      tft.fillRoundRect(12,y,296,36,0,bg);
      String name;
      if(i<historyFileCount) name=historyFiles[i].substring(1);
      else if(historyHasOlder && i==historyFileCount) name=T(S_OLDER,currentLanguage);
      else name=T(S_NEWER,currentLanguage);
      u8g2.setForegroundColor(COL_TEXT); u8g2.setBackgroundColor(bg);
      printFit(name,24,y+23,270);
    }
  }
}

const char* HT(history_text::Id id) { return history_text::get(id,currentLanguage); }

// A bounded, UTF-8-aware document renderer. Only one line is held in RAM;
// UP/DOWN scroll a page. Advice is wrapped in full, never silently ellipsized.
void historyTextLine(const char* text, uint16_t color) {
  if(historyTextCursor>=historyTextScroll && historyTextCursor<historyTextScroll+HISTORY_TEXT_ROWS) {
    u8g2.setForegroundColor(color); u8g2.setCursor(14,80+12*(historyTextCursor-historyTextScroll));
    u8g2.print(text);
  }
  ++historyTextCursor;
}

void historyParagraph(const char* text, uint16_t color) {
  const char* p=text;
  while(*p) {
    while(*p==' ') ++p;
    if(!*p) break;
    char line[160]; unsigned used=0, split=0;
    const char* start=p; const char* afterSpace=nullptr;
    while(*p && *p!='\n') {
      unsigned char c=static_cast<unsigned char>(*p);
      unsigned bytes=c<0x80?1:((c&0xe0)==0xc0?2:((c&0xf0)==0xe0?3:4));
      if(used+bytes>=sizeof(line)) break;
      memcpy(line+used,p,bytes); line[used+bytes]=0;
      if(u8g2.getUTF8Width(line)>286) { line[used]=0; break; }
      if(*p==' ') { split=used; afterSpace=p+1; }
      used+=bytes; p+=bytes;
    }
    if(*p && *p!='\n' && afterSpace) { used=split; p=afterSpace; }
    // All supplied UTF-8 glyphs fit on a line; protect against zero progress.
    if(p==start) { ++p; continue; }
    line[used]=0; historyTextLine(line,color);
    if(*p=='\n') ++p;
  }
  historyTextLine("",color);
}

void historyMetricStats(cardio::Metric which) {
  const auto& m=historyAnalysis.metric(which);
  char text[160];
  if(m.samples) {
    char mean[24]="--";
    if(isfinite(m.mean())) snprintf(mean,sizeof(mean),"%.1f",m.mean());
    snprintf(text,sizeof(text),"%s: %.1f / %s / %.1f %s",HT(history_text::Range),
      m.minimum,mean,m.maximum,which==cardio::Metric::Oxygen?"%":"bpm");
    historyParagraph(text,COL_TEXT);
  }
  snprintf(text,sizeof(text),"%s: %.1f | %s: %.0f",HT(history_text::ValidTime),
    double(m.validMs)/1000,HT(history_text::Coverage),m.coveragePercent());
  historyParagraph(text,COL_TEXT_DIM);
  if(which!=cardio::Metric::Oxygen) {
    snprintf(text,sizeof(text),"%s: %.1f; %s: %.1f",HT(history_text::HighTime),
      double(m.highMs)/1000,HT(history_text::LowTime),double(m.lowMs)/1000);
    historyParagraph(text,COL_TEXT_DIM);
  }
  historyParagraph(HT(m.sufficient()?history_text::Enough:history_text::NotEnough),COL_TEXT_DIM);
}

void historyRateAnalysis(cardio::Metric which) {
  using namespace history_text;
  const auto& m=historyAnalysis.metric(which);
  historyParagraph(HT(which==cardio::Metric::Ecg?Ecg:Pulse),COL_ACCENT);
  historyMetricStats(which);
  cardio::Finding finding=m.finding(historyAdultRest);
  if(!m.samples) historyParagraph(HT(NoEstimate),COL_WARN);
  else if(!historyAdultRest) historyParagraph(HT(ContextUnknown),COL_TEXT);
  else {
    Id explanation=RateUncertain;
    switch(finding) {
      case cardio::Finding::WithinReference: explanation=WithinRate; break;
      case cardio::Finding::HighRate: explanation=HighRate; break;
      case cardio::Finding::LowRate: explanation=LowRate; break;
      case cardio::Finding::MixedRate: explanation=MixedRate; break;
      default: break;
    }
    historyParagraph(HT(explanation),finding==cardio::Finding::WithinReference?COL_TEXT:COL_WARN);
    if(m.highMs) historyParagraph(HT(FastExplanation),COL_TEXT);
    if(m.lowMs) historyParagraph(HT(SlowExplanation),COL_TEXT);
  }
  historyParagraph(HT(RepeatRate),COL_TEXT);
  historyParagraph(HT(which==cardio::Metric::Ecg?EcgLimits:PulseLimits),COL_TEXT_DIM);
}

void historyOxygenAnalysis() {
  using namespace history_text;
  const auto& m=historyAnalysis.metric(cardio::Metric::Oxygen);
  historyParagraph(HT(Oxygen),COL_ACCENT); historyMetricStats(cardio::Metric::Oxygen);
  cardio::Finding f=m.finding(historyAdultRest);
  Id explanation=OxygenUncertain;
  if(!m.samples) explanation=NoEstimate;
  else if(f==cardio::Finding::WithinReference) explanation=OxygenUsual;
  else if(f==cardio::Finding::BorderlineOxygen) explanation=OxygenBorderline;
  else if(f==cardio::Finding::LowOxygen) explanation=OxygenLow;
  else if(f==cardio::Finding::VeryLowOxygen) explanation=OxygenVeryLow;
  historyParagraph(HT(explanation),f==cardio::Finding::VeryLowOxygen?COL_BAD:COL_TEXT);
  if(f==cardio::Finding::VeryLowOxygen) historyParagraph(HT(OxygenUrgent),COL_BAD);
  else if(f==cardio::Finding::LowOxygen) historyParagraph(HT(OxygenContact),COL_WARN);
  if(f!=cardio::Finding::VeryLowOxygen && f!=cardio::Finding::LowOxygen)
    historyParagraph(HT(RepeatOxygen),COL_TEXT);
  historyParagraph(HT(OxygenLimits),COL_TEXT_DIM);
}

void drawHistoryDetail() {
  using namespace history_text;
  clearDisplay(); drawHeader(S_HISTORY_TITLE);
  u8g2.setFont(FONT_SMALL); u8g2.setBackgroundColor(COL_BG);
  const Id tabs[]={Summary,Analysis,Help};
  for(int i=0;i<3;++i) {
    uint16_t bg=historyTab==i?COL_CARD_SEL:COL_CARD;
    tft.fillRoundRect(10+i*102,37,96,23,0,bg);
    u8g2.setForegroundColor(historyTab==i?COL_ACCENT:COL_TEXT_DIM);
    printFit(HT(tabs[i]),18+i*102,53,83);
  }
  historyTextCursor=0;
  if(historyTab==2) {
    historyParagraph(HT(Emergency),COL_BAD);
    historyParagraph(HT(NoDelay),COL_BAD);
    historyParagraph(HT(OxygenUrgent),COL_BAD);
    historyParagraph(HT(OxygenContact),COL_WARN);
    historyParagraph(HT(Historical),COL_TEXT);
    historyParagraph(HT(InterpretationLimits),COL_TEXT_DIM);
  } else if(historyContextPending) {
    historyParagraph(HT(ContextTitle),COL_TEXT);
    historyParagraph(HT(ContextNotice),COL_TEXT_DIM);
    // Choices always stay visible below the compact context explanation.
    tft.fillRect(0,157,320,55,COL_BG);
    for(int i=0;i<2;++i) {
      tft.fillRoundRect(10,158+i*26,300,24,0,historyContextChoice==i?COL_CARD_SEL:COL_CARD);
      u8g2.setForegroundColor(historyContextChoice==i?COL_ACCENT:COL_TEXT_DIM);
      printFit(HT(i?OtherContext:AdultRest),18,174+i*26,280);
    }
  } else if(historyLoading) {
    historyParagraph(HT(Reading),COL_TEXT);
    historyParagraph(HT(Emergency),COL_BAD);
  } else if(!historyAnalysis.supported()) {
    historyParagraph(HT(Unreadable),COL_WARN);
    historyParagraph(HT(Emergency),COL_BAD);
  } else if(historyTab==0) {
    historyParagraph(historyDetailFile.c_str(),COL_ACCENT);
    if(historyAnalysis.ioError()) historyParagraph(HT(ReadError),COL_WARN);
    char text[160];
    snprintf(text,sizeof(text),"%s: %.1f",HT(Duration),double(historyAnalysis.durationMs())/1000);
    historyParagraph(text,COL_TEXT);
    if(historyCategory==0) {
      historyParagraph(HT(Ecg),COL_ACCENT); historyMetricStats(cardio::Metric::Ecg);
    }
    historyParagraph(HT(Pulse),COL_ACCENT); historyMetricStats(cardio::Metric::Pulse);
    historyParagraph(HT(Oxygen),COL_ACCENT); historyMetricStats(cardio::Metric::Oxygen);
    snprintf(text,sizeof(text),"%s: %lu / %lu",HT(Damaged),
      (unsigned long)historyAnalysis.malformedRows(),(unsigned long)historyAnalysis.gapCount());
    historyParagraph(text,COL_TEXT_DIM);
    historyParagraph(HT(QualityExplanation),COL_TEXT_DIM);
  } else {
    historyParagraph(HT(Experimental),COL_WARN);
    if(historyAnalysis.ioError()) historyParagraph(HT(ReadError),COL_WARN);
    historyParagraph(HT(historyAdultRest?AdultRest:OtherContext),COL_ACCENT);
    // Put oxygen warnings before longer explanations of rates; do not bury urgency.
    const auto& oxygen=historyAnalysis.metric(cardio::Metric::Oxygen);
    if(oxygen.warningVeryLowOxygen()) {
      historyParagraph(HT(OxygenVeryLow),COL_BAD);
      historyParagraph(HT(OxygenUrgent),COL_BAD);
    } else if(oxygen.warningLowOxygen()) {
      historyParagraph(HT(OxygenLow),COL_WARN);
      historyParagraph(HT(OxygenContact),COL_WARN);
    }
    historyParagraph(HT(Historical),COL_TEXT_DIM);
    if(historyCategory==0) historyRateAnalysis(cardio::Metric::Ecg);
    historyRateAnalysis(cardio::Metric::Pulse);
    historyOxygenAnalysis();
    historyParagraph(HT(QualityExplanation),COL_TEXT_DIM);
    historyParagraph(HT(InterpretationLimits),COL_TEXT_DIM);
  }
  historyTextLines=historyTextCursor;
  u8g2.setForegroundColor(COL_TEXT_DIM);
  if(!historyContextPending) {
    char page[24]; snprintf(page,sizeof(page),"%u / %u",historyTextScroll/HISTORY_TEXT_ROWS+1,
      (historyTextLines+HISTORY_TEXT_ROWS-1)/HISTORY_TEXT_ROWS);
    printFit(page,260,218,50);
  }
}

// ============================================================
// 14. WI-FI: СКАНИРОВАНИЕ, СПИСОК, КЛАВИАТУРА ВВОДА ПАРОЛЯ
// ============================================================
int rssiToBars(int rssi) {
  if (rssi > -55) return 4;
  if (rssi > -65) return 3;
  if (rssi > -75) return 2;
  if (rssi > -85) return 1;
  return 0;
}

void drawWifiSpinner() {}

void drawWifiCaption(StringID id) {
  tft.fillRect(0, 150, SCREEN_W, 20, COL_BG);
  u8g2.setFont(FONT_SMALL);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COL_TEXT_DIM);
  u8g2.setBackgroundColor(COL_BG);
  int16_t tw = u8g2.getUTF8Width(T(id, currentLanguage));
  u8g2.setCursor((SCREEN_W - tw) / 2, 165);
  u8g2.print(T(id, currentLanguage));
}

void drawWifiList() {
  tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);

  int total = wifiNetCount + 1; // +1 за пункт "Обновить"
  int rowH = 42;

  for (int row = 0; row < WIFI_VISIBLE_ROWS; row++) {
    int idx = wifiListScroll + row;
    if (idx >= total) break;
    int y = HEADER_H + 8 + row * rowH;
    bool sel = (idx == wifiListSelected);
    uint16_t bg = sel ? COL_CARD_SEL : COL_CARD;
    uint16_t border = sel ? COL_ACCENT : COL_TRACK;
    tft.fillRoundRect(14, y, SCREEN_W - 28, rowH - 6,0, bg);
    tft.drawRoundRect(14, y, SCREEN_W - 28, rowH - 6, 8, border);

    if (idx == wifiNetCount) {
      u8g2.setFont(FONT_SMALL);
      u8g2.setFontMode(1);
      u8g2.setForegroundColor(sel ? COL_TEXT : COL_TEXT_DIM);
      u8g2.setBackgroundColor(bg);
      u8g2.setCursor(26, y + 24);
      u8g2.print(T(S_WIFI_RESCAN, currentLanguage));
      continue;
    }

    u8g2.setFont(FONT_SMALL);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(sel ? COL_TEXT : COL_TEXT_DIM);
    u8g2.setBackgroundColor(bg);
    printFit(wifiSSID[idx],26,y+24,268);
  }
}

void drawKeyboardScreen() {
  tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);

  tft.drawRoundRect(20, 44, SCREEN_W - 40, 30, 6, COL_TRACK);
  int showFrom = wifiPassLen > 20 ? wifiPassLen - 20 : 0;
  tft.setTextSize(2);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(28, 52);
  for (int i=showFrom; i<wifiPassLen; ++i) tft.print("*");

  char curCh[5], prevCh[5], nextCh[5];
  kbLabelFor(kbIndex, curCh);
  kbLabelFor((kbIndex - 1 + KB_TOTAL) % KB_TOTAL, prevCh);
  kbLabelFor((kbIndex + 1) % KB_TOTAL, nextCh);

  tft.fillRect(0, 90, SCREEN_W, 40, COL_BG);

  tft.setTextSize(2);
  tft.setTextColor(COL_TEXT_DIM, COL_BG);
  tft.setCursor(SCREEN_W / 2 - 70, 108);
  tft.print(prevCh);

  int16_t bx, by; uint16_t bw, bh;
  tft.setTextSize(4);
  tft.getTextBounds(curCh, 0, 0, &bx, &by, &bw, &bh);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setCursor(SCREEN_W / 2 - bw / 2, 96);
  tft.print(curCh);

  tft.setTextSize(2);
  tft.setTextColor(COL_TEXT_DIM, COL_BG);
  tft.setCursor(SCREEN_W / 2 + 50, 108);
  tft.print(nextCh);

  u8g2.setFont(FONT_SMALL);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COL_TEXT_DIM);
  u8g2.setBackgroundColor(COL_BG);
  u8g2.setCursor(16, 195);
  u8g2.print(T(S_WIFI_ENTER_PASSWORD, currentLanguage));
}

void drawWifiResult() {
  tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);

  u8g2.setFont(FONT_MED);
  u8g2.setFontMode(1);
  u8g2.setBackgroundColor(COL_BG);

  if (wifiConnectOk) {
    u8g2.setForegroundColor(COL_GOOD);
    int16_t tw = u8g2.getUTF8Width(T(S_WIFI_CONNECTED, currentLanguage));
    u8g2.setCursor((SCREEN_W - tw) / 2, 90);
    u8g2.print(T(S_WIFI_CONNECTED, currentLanguage));

    IPAddress ip = WiFi.localIP();
    char ipBuf[20];
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    int16_t bx, by; uint16_t bw, bh;
    tft.setTextSize(2);
    tft.getTextBounds(ipBuf, 0, 0, &bx, &by, &bw, &bh);
    tft.setTextColor(COL_TEXT_DIM, COL_BG);
    tft.setCursor((SCREEN_W - bw) / 2, 130);
    tft.print(ipBuf);
  } else {
    u8g2.setForegroundColor(COL_BAD);
    int16_t tw = u8g2.getUTF8Width(T(S_WIFI_FAILED, currentLanguage));
    u8g2.setCursor((SCREEN_W - tw) / 2, 110);
    u8g2.print(T(S_WIFI_FAILED, currentLanguage));
  }

  u8g2.setFont(FONT_SMALL);
  u8g2.setForegroundColor(COL_TEXT_DIM);
  u8g2.setCursor(16, 195);
}

void drawWifiScreen() {
  clearDisplay();
  drawHeader(S_WIFI_LABEL);
  switch (wifiState) {
    case WIFI_UI_SCANNING:   drawWifiSpinner(); drawWifiCaption(S_WIFI_SCANNING);   break;
    case WIFI_UI_LIST:       drawWifiList();                                       break;
    case WIFI_UI_PASSWORD:   drawKeyboardScreen();                                 break;
    case WIFI_UI_CONNECTING: drawWifiSpinner(); drawWifiCaption(S_WIFI_CONNECTING); break;
    case WIFI_UI_RESULT:     drawWifiResult();                                     break;
  }
}

void attemptWifiConnect(const String &ssid, const String &pass) {
  WiFi.disconnect();
  if (pass.length() == 0) WiFi.begin(ssid.c_str());
  else WiFi.begin(ssid.c_str(), pass.c_str());
}

void finishWifiScan(int n) {
  int count = max(0, n);
  if (count > MAX_WIFI_NETS) count = MAX_WIFI_NETS;
  for (int i = 0; i < count; i++) {
    wifiSSID[i] = WiFi.SSID(i);
    wifiRSSI[i] = WiFi.RSSI(i);
    wifiEnc[i] = WiFi.encryptionType(i);
  }
  WiFi.scanDelete();

  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - 1 - i; j++) {
      if (wifiRSSI[j] < wifiRSSI[j + 1]) {
        String ts = wifiSSID[j]; wifiSSID[j] = wifiSSID[j + 1]; wifiSSID[j + 1] = ts;
        int tr = wifiRSSI[j]; wifiRSSI[j] = wifiRSSI[j + 1]; wifiRSSI[j + 1] = tr;
        int te = wifiEnc[j]; wifiEnc[j] = wifiEnc[j + 1]; wifiEnc[j + 1] = te;
      }
    }
  }

  wifiNetCount = count;
  wifiListSelected = 0;
  wifiListScroll = 0;
  wifiState = WIFI_UI_LIST;
  drawWifiScreen();
}

void startWifiScan() {
  wifiEnabled = true; prefs.putBool("wifi_on", true);
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  wifiState = WIFI_UI_SCANNING;
  WiFi.scanNetworks(true);
  drawWifiScreen();
}

void moveWifiSelection(int delta) {
  int total = wifiNetCount + 1;
  wifiListSelected = (wifiListSelected + delta + total) % total;
  if (wifiListSelected < wifiListScroll) wifiListScroll = wifiListSelected;
  if (wifiListSelected >= wifiListScroll + WIFI_VISIBLE_ROWS) wifiListScroll = wifiListSelected - WIFI_VISIBLE_ROWS + 1;
  drawWifiList();
}

void onWifiListOk() {
  if (wifiListSelected == wifiNetCount) {
    startWifiScan();
    return;
  }
  selectedNetworkIndex = wifiListSelected;
  memset(wifiPassBuf, 0, sizeof(wifiPassBuf)); wifiPassLen = 0;
  if (wifiEnc[selectedNetworkIndex] == WIFI_AUTH_OPEN) {
    attemptWifiConnect(wifiSSID[selectedNetworkIndex], String(""));
    wifiState = WIFI_UI_CONNECTING;
    connectStartMs = millis();
    drawWifiScreen();
  } else {
    wifiPassLen = 0;
    wifiPassBuf[0] = '\0';
    kbIndex = 0;
    wifiState = WIFI_UI_PASSWORD;
    drawWifiScreen();
  }
}

void onKeyboardOk() {
  if (kbIndex == KB_SPECIAL_BACKSPACE) {
    if (wifiPassLen > 0) { wifiPassLen--; wifiPassBuf[wifiPassLen] = '\0'; }
    drawKeyboardScreen();
  } else if (kbIndex == KB_SPECIAL_ENTER) {
    attemptWifiConnect(wifiSSID[selectedNetworkIndex], String(wifiPassBuf));
    wifiState = WIFI_UI_CONNECTING;
    connectStartMs = millis();
    drawWifiScreen();
  } else {
    if (wifiPassLen < MAX_WIFI_PASS_LEN) {
      wifiPassBuf[wifiPassLen++] = KB_CHARS[kbIndex];
      wifiPassBuf[wifiPassLen] = '\0';
    }
    drawKeyboardScreen();
  }
}

void updateWifiScreen() {
  unsigned long now = millis();
  if (wifiState == WIFI_UI_SCANNING) {
    static unsigned long lastSpin = 0;
    if (now - lastSpin > 150) { lastSpin = now; drawWifiSpinner(); }
    int n = WiFi.scanComplete();
    if (n >= 0) {
      finishWifiScan(n);
    } else if (n == WIFI_SCAN_FAILED) {
      wifiNetCount = 0;
      wifiListSelected = 0;
      wifiListScroll = 0;
      wifiState = WIFI_UI_LIST;
      drawWifiScreen();
    }
  } else if (wifiState == WIFI_UI_CONNECTING) {
    static unsigned long lastSpin2 = 0;
    if (now - lastSpin2 > 150) { lastSpin2 = now; drawWifiSpinner(); }
    if (WiFi.status() == WL_CONNECTED) {
      prefs.putString("wifi_ssid", wifiSSID[selectedNetworkIndex]);
      prefs.putString("wifi_pass", String(wifiPassBuf));
      memset(wifiPassBuf, 0, sizeof(wifiPassBuf)); wifiPassLen = 0;
      wifiConnectOk = true;
      wifiState = WIFI_UI_RESULT;
      drawWifiScreen();
    } else if (now - connectStartMs > WIFI_CONNECT_TIMEOUT_MS) {
      WiFi.disconnect();
      wifiConnectOk = false;
      wifiState = WIFI_UI_RESULT;
      drawWifiScreen();
    }
  }
}

// ============================================================
// 15. КНОПКИ И НАВИГАЦИЯ
// ============================================================


// Кнопка "Назад" (TTP223 №4) подключена не к GPIO, а к каналу A2
// внешнего АЦП ADS1115 — читаем её как аналоговый уровень с порогом.


void onUp() {

  if(currentScreen==SCR_HISTORY_DETAIL) {
    if(historyContextPending) historyContextChoice=1-historyContextChoice;
    else historyTextScroll=historyTextScroll>=HISTORY_TEXT_ROWS?historyTextScroll-HISTORY_TEXT_ROWS:0;
    drawHistoryDetail(); return;
  }
  if (currentScreen == SCR_MAIN) {
    selectedItem = (selectedItem + MAIN_MENU_COUNT - 1) % MAIN_MENU_COUNT;
    drawMainMenu();
  } else if (currentScreen == SCR_SETTINGS) {
    settingsSelected = (settingsSelected + SETTINGS_COUNT - 1) % SETTINGS_COUNT;
    drawSettingsMenu();
  } else if (currentScreen == SCR_HISTORY) {
    historyMenuSelected = (historyMenuSelected + HISTORY_MENU_COUNT - 1) % HISTORY_MENU_COUNT;
    drawHistoryMenu();
  } else if (currentScreen == SCR_HISTORY_LIST) {
    if (!historyScanning && historyItemCount() > 0) {
      historyListSelected = (historyListSelected + historyItemCount() - 1) % historyItemCount();
      if (historyListSelected < historyListScroll) historyListScroll = historyListSelected;
      if (historyListSelected >= historyListScroll + HISTORY_VISIBLE_ROWS) historyListScroll = historyListSelected - HISTORY_VISIBLE_ROWS + 1;
      drawHistoryList();
    }
  } else if (currentScreen == SCR_WIFI) {
    if (wifiState == WIFI_UI_LIST) moveWifiSelection(-1);
    else if (wifiState == WIFI_UI_PASSWORD) { kbIndex = (kbIndex - 1 + KB_TOTAL) % KB_TOTAL; drawKeyboardScreen(); }
  }
}

void onDown() {

  if(currentScreen==SCR_HISTORY_DETAIL) {
    if(historyContextPending) historyContextChoice=1-historyContextChoice;
    else if(historyTextScroll+HISTORY_TEXT_ROWS<historyTextLines) historyTextScroll+=HISTORY_TEXT_ROWS;
    drawHistoryDetail(); return;
  }
  if (currentScreen == SCR_MAIN) {
    selectedItem = (selectedItem + 1) % MAIN_MENU_COUNT;
    drawMainMenu();
  } else if (currentScreen == SCR_SETTINGS) {
    settingsSelected = (settingsSelected + 1) % SETTINGS_COUNT;
    drawSettingsMenu();
  } else if (currentScreen == SCR_HISTORY) {
    historyMenuSelected = (historyMenuSelected + 1) % HISTORY_MENU_COUNT;
    drawHistoryMenu();
  } else if (currentScreen == SCR_HISTORY_LIST) {
    if (!historyScanning && historyItemCount() > 0) {
      historyListSelected = (historyListSelected + 1) % historyItemCount();
      if (historyListSelected < historyListScroll) historyListScroll = historyListSelected;
      if (historyListSelected >= historyListScroll + HISTORY_VISIBLE_ROWS) historyListScroll = historyListSelected - HISTORY_VISIBLE_ROWS + 1;
      drawHistoryList();
    }
  } else if (currentScreen == SCR_WIFI) {
    if (wifiState == WIFI_UI_LIST) moveWifiSelection(1);
    else if (wifiState == WIFI_UI_PASSWORD) { kbIndex = (kbIndex + 1) % KB_TOTAL; drawKeyboardScreen(); }
  }
}

void onOk() {
  if(currentScreen==SCR_HISTORY_DETAIL) {
    if(historyContextPending) {
      historyAdultRest=historyContextChoice==0;
      historyContextPending=false; historyContextChosen=true;
    } else {
      historyTab=(historyTab+1)%3;
      historyContextPending=historyTab==1 && !historyContextChosen && !historyLoading && historyAnalysis.supported();
    }
    historyTextScroll=0; drawHistoryDetail(); return;
  }
  if (currentScreen==SCR_FORGET) {
    WiFi.disconnect(false, true); prefs.remove("wifi_ssid"); prefs.remove("wifi_pass");
    currentScreen=SCR_SETTINGS; drawSettingsMenu(); return;
  }
  if (currentScreen==SCR_DIAGNOSTICS) { mountSd(); drawDiagnostics(); return; }
  if (currentScreen == SCR_MAIN) {
    // Явный switch вместо арифметики selectedItem+1 — устойчиво к
    // изменению количества/порядка пунктов главного меню.
    switch (selectedItem) {
      case 0: currentScreen = SCR_ECG;      enterEcgScreen();    break;
      case 1: currentScreen = SCR_PULSE;    enterPulseScreen();  break;
      case 2: currentScreen = SCR_SPO2;     enterSpo2Screen();   break;
      case 3: currentScreen = SCR_HISTORY;  enterHistoryScreen();break;
      case 4: currentScreen = SCR_SETTINGS; drawSettingsMenu();  break;
    }
  } else if (currentScreen == SCR_SETTINGS) {
    if(settingsSelected==0) {currentScreen=SCR_WIFI;startWifiScan();}
    else if(settingsSelected==2) cycleLanguage();
    // Bluetooth has no transport implementation yet; its status is read-only.
  } else if (currentScreen == SCR_HISTORY) {
    historyCategory = historyMenuSelected;
    scanHistoryFiles(historyCategory);
    currentScreen = SCR_HISTORY_LIST;
    drawHistoryList();
  } else if (currentScreen == SCR_HISTORY_LIST) {
    if(historyScanning) return;
    if(historyFileCount && historyListSelected>=historyFileCount) {
      bool older=historyHasOlder && historyListSelected==historyFileCount;
      historyDirection=older?1:-1;
      historyBoundary=older?historyIds[historyFileCount-1]:historyIds[0];
      startHistoryScan(); drawHistoryList(); return;
    }
    if (historyFileCount > 0) {
      computeHistoryDetail(historyFiles[historyListSelected]);
      currentScreen = SCR_HISTORY_DETAIL;
      drawHistoryDetail();
    }
  } else if (currentScreen == SCR_WIFI) {
    if (wifiState == WIFI_UI_LIST) onWifiListOk();
    else if (wifiState == WIFI_UI_PASSWORD) onKeyboardOk();
    else if (wifiState == WIFI_UI_RESULT) { wifiState = WIFI_UI_LIST; drawWifiScreen(); }
  }
}

void onBack() {
  if (currentScreen==SCR_DIAGNOSTICS || currentScreen==SCR_FORGET) {
    currentScreen=SCR_SETTINGS; drawSettingsMenu(); return;
  }
  if (currentScreen == SCR_WIFI) {
    if (wifiState == WIFI_UI_LIST || wifiState == WIFI_UI_SCANNING) {
      currentScreen = SCR_SETTINGS;
      drawSettingsMenu();
    } else if (wifiState == WIFI_UI_PASSWORD) {
      wifiState = WIFI_UI_LIST;
      drawWifiScreen();
    } else if (wifiState == WIFI_UI_RESULT) {
      currentScreen = SCR_SETTINGS;
      drawSettingsMenu();
    }
    else if (wifiState==WIFI_UI_CONNECTING) {
      WiFi.disconnect(); wifiState=WIFI_UI_LIST; drawWifiScreen();
    }
    if (currentScreen!=SCR_WIFI) WiFi.scanDelete();
    memset(wifiPassBuf,0,sizeof(wifiPassBuf)); wifiPassLen=0;
  } else if (currentScreen == SCR_HISTORY_DETAIL) {
    historyLoading=false; if(historyReader) historyReader.close();
    currentScreen = SCR_HISTORY_LIST;
    drawHistoryList();
  } else if (currentScreen == SCR_HISTORY_LIST) {
    historyScanning=false; if(historyDirectory) historyDirectory.close();
    currentScreen = SCR_HISTORY;
    drawHistoryMenu();
  } else if (currentScreen == SCR_HISTORY) {
    currentScreen = SCR_MAIN;
    drawMainMenu();
  } else if (currentScreen != SCR_MAIN) {
    if (currentScreen == SCR_ECG || currentScreen == SCR_PULSE || currentScreen == SCR_SPO2) {
      sensorCommand.store((sessionCommand += 4) & ~3u);
      closeLogFile();
    }
    currentScreen = SCR_MAIN;
    drawMainMenu();
  }
}

// ============================================================
// 16. SETUP / LOOP
// ============================================================
void drawSplash() {
  clearDisplay();

  u8g2.setFont(FONT_MED);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(COL_TEXT);
  u8g2.setBackgroundColor(COL_BG);
  int16_t tw = u8g2.getUTF8Width(T(S_APP_TITLE, currentLanguage));
  u8g2.setCursor((SCREEN_W - tw) / 2, SCREEN_H / 2 + 40);
  u8g2.print(T(S_APP_TITLE, currentLanguage));
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_UP,INPUT); pinMode(BTN_DOWN,INPUT); pinMode(BTN_OK,INPUT);
  // Deselect both SPI devices before either library initializes the bus.
  pinMode(TFT_CS,OUTPUT); digitalWrite(TFT_CS,HIGH);
  pinMode(SD_CS_PIN,OUTPUT); digitalWrite(SD_CS_PIN,HIGH);
  prefs.begin("medmon",false);
  currentLanguage=Language(prefs.getUChar("lang",LANG_RU));
  if (currentLanguage>=LANG_COUNT) currentLanguage=LANG_RU;
  recordingEnabled=prefs.getBool("record",true);
  wifiEnabled=prefs.getBool("wifi_on",true);
  ppgAutoEnabled.store(prefs.getBool("ppg_auto",true));
  ecgGain=prefs.getUChar("gain",1)%4;
  fineGrid=prefs.getBool("fine_grid",true);
  ecgMains=prefs.getUChar("mains",50);
  if(ecgMains!=50 && ecgMains!=60) ecgMains=0;
  SPI.begin(SPI_SCK,SPI_MISO,SPI_MOSI,TFT_CS);
  tft.begin(); tft.setSPISpeed(27000000); tft.setRotation(3); tft.setTextWrap(false);
  u8g2.begin(tft); drawSplash();
  mountSd();
  Wire.begin(I2C_SDA,I2C_SCL); Wire.setClock(400000); Wire.setTimeOut(15);
  startAcquisition();
  WiFi.persistent(false); WiFi.setAutoReconnect(true);
  if (wifiEnabled) { WiFi.mode(WIFI_STA); connectSavedWifi(); } else WiFi.mode(WIFI_OFF);
  drawMainMenu();
}

void loop() {
  consumeSamples();
  uint32_t now=millis();
  const bool keyboard=currentScreen==SCR_WIFI && wifiState==WIFI_UI_PASSWORD;
  bool up=buttons[0].update(digitalRead(BTN_UP)==HIGH,now,true,keyboard);
  bool down=buttons[1].update(digitalRead(BTN_DOWN)==HIGH,now,true,keyboard);
  bool ok=buttons[2].update(digitalRead(BTN_OK)==HIGH,now);
  bool back=buttons[3].update(sensorHealth.back,now,sensorHealth.ads);
  // Emergency navigation when ADS1115 (and therefore BACK) is unavailable.
  static uint32_t chordStart=0; static bool chordUsed=false;
  bool chord=digitalRead(BTN_UP)==HIGH && digitalRead(BTN_DOWN)==HIGH;
  if (chord) {
    if (!chordStart) chordStart=now;
    if (!chordUsed && now-chordStart>=1200) { onBack(); chordUsed=true; }
  } else { chordStart=0; chordUsed=false; }
  if (!chord) {
    if (back) onBack(); else if (ok) onOk(); else if(up) onUp(); else if(down) onDown();
  }
  serviceHistoryScan();
  serviceHistory();
  if (logFileOpen && (logUsed>=3072 || now-lastLogFlush>=1000)) {
    flushLogBuffer(); if(logFileOpen) logFile.flush(); lastLogFlush=now;
  }
  now=millis();
  poxProcessor.expire(now); ecgDetector.expire(now);
  if (now-lastUi>=40) {
    lastUi=now; updateHeaderStatus();
    switch(currentScreen) {
      case SCR_ECG: renderECG(); break;
      case SCR_PULSE: renderPulseOnly(); break;
      case SCR_SPO2: renderSpo2Only(); break;
      case SCR_WIFI: updateWifiScreen(); break;
      case SCR_DIAGNOSTICS: {
        static uint32_t lastDiagnostics=0;
        if(now-lastDiagnostics>=1000){lastDiagnostics=now;drawDiagnostics();}
        break;
      }
      default: break;
    }
  }
  delay(1);
}

// Shared UI and bounded storage services.
void printFit(String text, int x, int baseline, int width) {
  bool shortened=false;
  while (text.length() && u8g2.getUTF8Width((text + (shortened ? "..." : "")).c_str())>width) {
    int i=text.length()-1;
    while (i>0 && (uint8_t(text[i]) & 0xc0)==0x80) --i;
    text.remove(i); shortened=true;
  }
  u8g2.setCursor(x,baseline); u8g2.print(text);
  if(shortened) u8g2.print("...");
}
void connectSavedWifi() {
  String ssid=prefs.getString("wifi_ssid", "");
  if (ssid.length()) WiFi.begin(ssid.c_str(),prefs.getString("wifi_pass", "").c_str());
}
void mountSd() {
  if (logFileOpen || historyLoading || historyScanning) return;
  SD.end(); digitalWrite(TFT_CS,HIGH);
  sdAvailable=SD.begin(SD_CS_PIN,SPI,4000000);
  sdError=!sdAvailable;
  sdNextSequence=1;
  if(sdAvailable) {
    File directory=SD.open("/");
    while(directory) {
      File f=directory.openNextFile(); if(!f) break;
      String name=f.name(); bool isDirectory=f.isDirectory(); f.close();
      if(isDirectory || !name.endsWith(".csv")) continue;
      if(name.startsWith("/")) name=name.substring(1);
      if(!name.startsWith("ecg_") && !name.startsWith("pulse_") && !name.startsWith("spo2_")) continue;
      String digits=name.substring(name.lastIndexOf('_')+1,name.length()-4);
      bool valid=digits.length()>0 && digits.length()<=9;
      for(size_t i=0;i<digits.length();++i) valid=valid && isDigit(digits[i]);
      if(valid) sdNextSequence=max(sdNextSequence,uint32_t(strtoul(digits.c_str(),nullptr,10)+1));
      delay(1);
    }
    if(directory) directory.close();
  }
}
void flushLogBuffer() {
  if (!logFileOpen || !logUsed) return;
  size_t written=logFile.write(reinterpret_cast<uint8_t*>(logBuffer),logUsed);
  if (written!=logUsed || logFile.getWriteError()) {
    // Do not retry/duplicate a partial CSV record or consume internal flash.
    sdError=true; sdAvailable=false; logFile.close(); logFileOpen=false;
  }
  logUsed=0;
}
void appendLog(const char* row) {
  if (!logFileOpen) return;
  size_t n=strlen(row);
  if (n>sizeof(logBuffer)) return;
  if(logUsed+n>sizeof(logBuffer)) flushLogBuffer();
  if(!logFileOpen) return;
  memcpy(logBuffer+logUsed,row,n); logUsed+=n;
}
void beginMeasurement(int mode) {
  displayPulseStats.reset();lastDisplayStats=millis();
  poxProcessor.reset(); ecgDetector.setMains(ecgMains);
  havePpgEpoch=false;
  ppgBaselineInit=false; graphHasSample=false; ppgPlotConnected=false;
  sessionStart=millis();
  sessionCommand=((sessionCommand+4)&~3u)|uint32_t(mode);
}
void addGraphSample(float v) {
  if (!graphHasSample) graphLow=graphHigh=v;
  else { graphLow=fminf(graphLow,v); graphHigh=fmaxf(graphHigh,v); }
  graphHasSample=true;
}
void consumeSamples() {
  if (!acquisitionStarted) return;
  SensorHealth health;
  if (xQueueReceive(healthQueue,&health,0)==pdTRUE) {
    if(health.dropped!=sensorHealth.dropped || health.faults!=sensorHealth.faults) {
      poxProcessor.reset(); ecgDetector.reset();
      appendLog("# acquisition_gap\n");
    }
    sensorHealth=health;
  }
  SensorSample sample;
  for (int n=0; n<384 && xQueueReceive(sampleQueue,&sample,0)==pdTRUE; ++n) {
    if(sample.command!=sessionCommand || (sample.command&3)==0) continue;
    if(millis()-sample.ms>300) {
      poxProcessor.reset(); ecgDetector.reset(); graphHasSample=false; ppgPlotConnected=false; continue;
    }
    char row[192], metric[20]="", secondary[20]="";
    if(sample.kind==2) { poxProcessor.reset(); ppgBaselineInit=false; appendLog("# ppg_gap\n"); continue; }
    if(sample.kind==1 || sample.kind==3 || sample.kind==4) {
      if(!havePpgEpoch || activePpgEpoch!=sample.exposureEpoch) {
        activePpgEpoch=sample.exposureEpoch; havePpgEpoch=true;
        poxProcessor.reset(); ppgBaselineInit=false; graphHasSample=false; ppgPlotConnected=false;
        snprintf(row,sizeof(row),"# ppg_gap\n# ppg_epoch=%u\n",activePpgEpoch); appendLog(row);
      }
      if(sample.kind==4) {
        snprintf(row,sizeof(row),"# ppg_led,ms=%lu,red=%lu,ir=%lu,epoch=%u\n",
          (unsigned long)(sample.ms-sessionStart),(unsigned long)sample.a,(unsigned long)sample.b,activePpgEpoch);
        appendLog(row); continue;
      }
    }
    if(sample.kind==0 && (currentScreen==SCR_ECG || currentScreen==SCR_PULSE)) {
      latestEcg=constrain(int(sample.a)*4095/26400,0,4095);
      ecgDetector.update(float(sample.a)*4095.0f/26400.0f,sample.ms);
      ecgPlot.push(sample.ms,ecgDetector.filtered,ecgDetector.quality);
      if(currentScreen!=SCR_ECG)continue;
      if(ecgDetector.bpm>0) snprintf(metric,sizeof(metric),"%.1f",ecgDetector.bpm);
      snprintf(row,sizeof(row),"%lu,ecg,%lu,%.2f,%d,,,,,,,,%s\n",(unsigned long)(sample.ms-sessionStart),
               (unsigned long)sample.a,ecgDetector.filtered,ecgDetector.quality,metric);
      appendLog(row);
    }
    if((sample.kind==1 || sample.kind==3) && (currentScreen==SCR_PULSE || currentScreen==SCR_SPO2 || currentScreen==SCR_ECG)) {
      latestIr=sample.a;
      if(sample.kind==3) { poxProcessor.reset(); ppgBaselineInit=false; }
      else poxProcessor.update(sample.a,sample.b,sample.ms);
      if(currentScreen==SCR_ECG) {
        if(poxProcessor.hrValid) snprintf(metric,sizeof(metric),"%d",poxProcessor.beatAvg);
        if(poxProcessor.spo2Valid) snprintf(secondary,sizeof(secondary),"%.1f",poxProcessor.spo2);
        snprintf(row,sizeof(row),"%lu,ppg,,,,%lu,%lu,%d,%d,%.3f,%s,%s,\n",
          (unsigned long)(sample.ms-sessionStart),(unsigned long)sample.a,(unsigned long)sample.b,
          poxProcessor.hrValid,poxProcessor.spo2Valid,poxProcessor.correlation,metric,secondary);
        appendLog(row); continue;
      }
      if(poxProcessor.finger) {
        if(!ppgBaselineInit) { ppgBaseline=sample.a; ppgBaselineInit=true; }
        ppgBaseline+=0.12f*(sample.a-ppgBaseline);
        addGraphSample(sample.a-ppgBaseline);
      } else { ppgBaselineInit=false; addGraphSample(0); }
      if(currentScreen==SCR_PULSE) {
        if(poxProcessor.hrValid) snprintf(metric,sizeof(metric),"%d",poxProcessor.beatAvg);
        if(poxProcessor.spo2Valid) snprintf(secondary,sizeof(secondary),"%.1f",poxProcessor.spo2);
      } else {
        if(poxProcessor.spo2Valid) snprintf(metric,sizeof(metric),"%.1f",poxProcessor.spo2);
        if(poxProcessor.hrValid) snprintf(secondary,sizeof(secondary),"%d",poxProcessor.beatAvg);
      }
      snprintf(row,sizeof(row),"%lu,%lu,%lu,%d,%d,%.3f,%s,%s\n",(unsigned long)(sample.ms-sessionStart),
        (unsigned long)sample.a,(unsigned long)sample.b,poxProcessor.hrValid,poxProcessor.spo2Valid,
        poxProcessor.correlation,secondary,metric);
      appendLog(row);
    }
  }
}
StringID pulseState() {
  if(!sensorHealth.max || !acquisitionStarted || millis()-sensorHealth.ppgMs>500) return S_SENSOR_MISSING;
  if(ppgAutoEnabled.load()) {
    switch(sensorHealth.exposure) {
      case ppg::ExposureState::Searching: return S_PLACE_FINGER;
      case ppg::ExposureState::Adjusting: return S_PPG_TUNING;
      case ppg::ExposureState::Weak: return S_PPG_WEAK;
      case ppg::ExposureState::Clipped: return S_PPG_CLIPPED;
      case ppg::ExposureState::Unstable: return S_PPG_UNSTABLE;
      default: break;
    }
  }
  if(!poxProcessor.finger) return S_PLACE_FINGER;
  if(poxProcessor.count<BUFFER_SIZE || poxProcessor.goodWindows==1) return S_COLLECTING;
  if(!poxProcessor.hrValid || (currentScreen==SCR_SPO2 && !poxProcessor.spo2Valid)) return S_POOR_SIGNAL;
  return S_READY;
}
void drawGraphGrid(int top,int bottom) {
  tft.fillRect(0,top,320,bottom-top,COL_PAPER);
  for(int x=0;x<320;x+=fineGrid?4:20)tft.drawFastVLine(x,top,bottom-top,x%20==0?COL_PAPER_BOLD:COL_PAPER_FINE);
  for(int y=top;y<bottom;y+=fineGrid?4:20)tft.drawFastHLine(0,y,320,(y-top)%20==0?COL_PAPER_BOLD:COL_PAPER_FINE);
  prevEcgY=(top+bottom)/2;ppgPlotConnected=false;
}
void drawWaveColumn(int top,int bottom,float low,float high) {
  int x=graphX;
  uint16_t grid=x%20==0?COL_PAPER_BOLD:(fineGrid && x%4==0?COL_PAPER_FINE:COL_PAPER);
  tft.drawFastVLine(x,top,bottom-top,grid);
  for(int y=top;y<bottom;y+=fineGrid?4:20)tft.drawPixel(x,y,(y-top)%20==0?COL_PAPER_BOLD:COL_PAPER_FINE);
  int y1=constrain(int((top+bottom)/2-high),top+1,bottom-2);
  int y2=constrain(int((top+bottom)/2-low),top+1,bottom-2);
  int mid=(y1+y2)/2;
  if(poxProcessor.finger && (!ppgAutoEnabled.load() || sensorHealth.exposure==ppg::ExposureState::Stable)) {
    if(x && ppgPlotConnected)tft.drawLine(x-1,prevEcgY,x,mid,COL_OXYGEN);
    tft.drawFastVLine(x,y1,max(1,y2-y1+1),COL_OXYGEN);
    ppgPlotConnected=true;
  } else ppgPlotConnected=false;
  prevEcgY=mid;graphX=(graphX+1)%320;
}
void drawMeasurementStatus(StringID state,int baseline) {
  static DisplayTextCache cache;char key[24];snprintf(key,sizeof(key),"%d/%d",int(state),baseline);
  if(!cache.update(key,displayRevision))return;
  uint16_t bg=state==S_READY?COL_CARD_SEL:COL_WARNING_BG;
  tft.fillRect(0,baseline-12,320,16,bg);
  tft.fillRect(0,baseline-12,3,16,state==S_READY?COL_ACCENT:COL_WARN);
  u8g2.setFont(FONT_SMALL);u8g2.setForegroundColor(state==S_READY?COL_ACCENT:COL_TEXT);
  printFit(T(state,currentLanguage),8,baseline,304);
}
void drawValue(int x,int y,int value,const char* unit) {
  uint16_t bg=currentScreen==SCR_SPO2?COL_CARD:COL_BG;
  tft.fillRect(x,y,140,32,bg);
  tft.setTextSize(3); tft.setTextColor(COL_TEXT,bg); tft.setCursor(x,y);
  if(value<0) tft.print("--"); else tft.print(value);
  tft.setTextSize(1); tft.setTextColor(COL_TEXT_DIM,bg); tft.setCursor(x+82,y+14); tft.print(unit);
}
void drawSignalQuality(int baseline) {
  tft.fillRect(14,baseline-12,292,16,COL_BG);
  tft.fillRoundRect(16,baseline-7,288,4,2,COL_TRACK);
  if(poxProcessor.count<BUFFER_SIZE) {
    int width=288*poxProcessor.count/BUFFER_SIZE;
    if(width>0) tft.fillRoundRect(16,baseline-7,width,4,2,COL_ACCENT_DIM);
  } else if(poxProcessor.hrValid) {
    tft.fillRoundRect(16,baseline-7,288,4,2,COL_ACCENT);
  }
}

void drawDiagnosticLine(String text,int baseline,int width) {
  static DisplayTextCache cache[8];int row=baseline>=210?7:(baseline-58)/22;
  if(!cache[row].update(text.c_str(),displayRevision))return;
  while(u8g2.getUTF8Width(text.c_str())<width-6)text+=" ";
  u8g2.setFontMode(0);u8g2.setBackgroundColor(COL_BG);
  printFit(text,14,baseline,width);u8g2.setFontMode(1);
}
void drawDiagnostics() {
  if(displayedScreen!=currentScreen){clearDisplay();drawHeader(S_DIAGNOSTICS);}
  u8g2.setFont(FONT_SMALL); u8g2.setForegroundColor(COL_TEXT);
  const char* missing=T(S_SENSOR_MISSING,currentLanguage);
  char line[128];
  snprintf(line,sizeof(line),"MAX30102: %s",sensorHealth.max?"OK":missing); drawDiagnosticLine(line,58,290);
  snprintf(line,sizeof(line),"ADS1115: %s",sensorHealth.ads?"OK":missing); drawDiagnosticLine(line,80,290);
  if(sdAvailable) snprintf(line,sizeof(line),"SD: %llu / %llu MB",SD.usedBytes()/1048576,SD.totalBytes()/1048576);
  else snprintf(line,sizeof(line),"SD: %s",T(S_HISTORY_NO_SD,currentLanguage));
  drawDiagnosticLine(line,102,290);
  snprintf(line,sizeof(line),"RAM: %lu KB / min %lu KB",(unsigned long)(ESP.getFreeHeap()/1024),
           (unsigned long)(ESP.getMinFreeHeap()/1024)); drawDiagnosticLine(line,124,290);
  snprintf(line,sizeof(line),"BAT: %.2f V  ~%d%%",readBatteryVoltage(),batteryPercent()); drawDiagnosticLine(line,146,290);
  snprintf(line,sizeof(line),"Lost: %lu  late: %lu  I2C: %lu",(unsigned long)sensorHealth.dropped,
           (unsigned long)sensorHealth.ecgLate,(unsigned long)sensorHealth.faults); drawDiagnosticLine(line,168,290);
  drawDiagnosticLine(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"Wi-Fi: --",190,290);
  snprintf(line,sizeof(line),"LED R/IR: %u/%u  AUTO: %s",sensorHealth.ledRed,sensorHealth.ledIr,
    ppgAutoEnabled.load()?"ON":"OFF"); drawDiagnosticLine(line,210,290);
  if(!acquisitionStarted)printFit("TASK ERROR: restart",14,228,292);
}
void serviceHistory() {
  if(!historyLoading || currentScreen!=SCR_HISTORY_DETAIL) return;
  bool readFailed=false;
  // Bounded byte budget, including broken/very long lines. No blocking
  // readBytesUntil or full-recording allocation. BACK remains responsive.
  uint8_t block[256];
  for(unsigned n=0;n<6 && historyBytesRead<historyExpectedBytes;++n) {
    size_t remaining=historyExpectedBytes-historyBytesRead;
    size_t requested=remaining<sizeof(block)?remaining:sizeof(block);
    int received=historyReader.read(block,requested);
    if(received>0) {
      historyBytesRead+=size_t(received);
      historyCsv.feed(block,size_t(received));
    }
    if(received!=int(requested)) { readFailed=true; historyAnalysis.markIoError(); break; }
  }
  static uint32_t lastDraw=0;
  if(readFailed || historyBytesRead==historyExpectedBytes) {
    historyReader.close(); historyLoading=false; historyCsv.finish();
    if(historyTab==1 && !historyContextChosen && historyAnalysis.supported()) historyContextPending=true;
    if(historyTab!=2) { historyTextScroll=0; drawHistoryDetail(); }
  } else if(millis()-lastDraw>500 && historyTab!=2) { lastDraw=millis(); drawHistoryDetail(); }
}

int historyItemCount() { return historyFileCount + int(historyHasOlder) + int(historyHasNewer); }
void startHistoryScan() {
  if(historyDirectory) historyDirectory.close();
  for(int i=0;i<HISTORY_MAX_FILES;++i) historyFiles[i]="";
  historyFileCount=0; historyListSelected=historyListScroll=0;
  historyCandidates=0; historyHasOlder=historyHasNewer=false;
  if(!sdAvailable) { historyScanning=false; return; }
  historyDirectory=SD.open("/"); historyScanning=bool(historyDirectory);
}
void serviceHistoryScan() {
  if(!historyScanning || currentScreen!=SCR_HISTORY_LIST) return;
  String prefix=String(HISTORY_PREFIXES[historyCategory])+"_";
  for(int entry=0;entry<8;++entry) {
    File f=historyDirectory.openNextFile();
    if(!f) {
      historyDirectory.close(); historyScanning=false;
      historyHasOlder=historyDirection==-1 || historyCandidates>HISTORY_MAX_FILES;
      historyHasNewer=historyDirection==1 || (historyDirection==-1 && historyCandidates>HISTORY_MAX_FILES);
      if(!historyFileCount) historyHasOlder=historyHasNewer=false;
      drawHistoryList(); return;
    }
    bool directory=f.isDirectory(); String name=f.name(); f.close();
    if(name.startsWith("/")) name=name.substring(1);
    if(directory || name.length()>40 || !name.startsWith(prefix) || !name.endsWith(".csv")) continue;
    String digits=name.substring(prefix.length(),name.length()-4);
    bool valid=digits.length()>0 && digits.length()<=9;
    for(size_t i=0;i<digits.length();++i) valid=valid && isDigit(digits[i]);
    if(!valid) continue;
    uint32_t id=strtoul(digits.c_str(),nullptr,10);
    if((historyDirection==1 && id>=historyBoundary) || (historyDirection==-1 && id<=historyBoundary)) continue;
    ++historyCandidates;
    int slot=historyFileCount;
    if(slot==HISTORY_MAX_FILES) {
      // Retain only the nearest page on either side of the boundary.
      slot=historyDirection==-1?0:HISTORY_MAX_FILES-1;
      if(historyDirection==-1 ? id>=historyIds[slot] : id<=historyIds[slot]) continue;
    } else ++historyFileCount;
    historyIds[slot]=id; historyFiles[slot]="/"+name;
    for(int i=0;i<historyFileCount-1;++i) for(int j=i+1;j<historyFileCount;++j) {
      if(historyIds[i]<historyIds[j]) {
        uint32_t idSwap=historyIds[i]; historyIds[i]=historyIds[j]; historyIds[j]=idSwap;
        String fileSwap=historyFiles[i]; historyFiles[i]=historyFiles[j]; historyFiles[j]=fileSwap;
      }
    }
  }
}

void drawCompactMetric(int x,const char* label,int value) {
  tft.fillRect(x,172,90,33,COL_BG);
  tft.setTextSize(1); tft.setTextColor(COL_TEXT_DIM,COL_BG); tft.setCursor(x,173); tft.print(label);
  tft.setTextSize(2); tft.setTextColor(COL_TEXT,COL_BG); tft.setCursor(x,187);
  if(value>=0) tft.print(value); else tft.print("--");
}
void eraseEcgPlotColumn(int x) {
  int top=ecgGraphTop(),bottom=ecgGraphBottom();
  uint16_t color=x%20==0?COL_PAPER_BOLD:(fineGrid && x%4==0?COL_PAPER_FINE:COL_PAPER);
  tft.drawFastVLine(x,top,bottom-top,color);
  for(int y=top;y<bottom;y+=fineGrid?4:20)tft.drawPixel(x,y,(y-top)%20==0?COL_PAPER_BOLD:color==COL_PAPER_BOLD?color:COL_PAPER_FINE);
}
void renderEcgWave() {
  int top=ecgGraphTop(),bottom=ecgGraphBottom();
  EcgPlotColumn column;
  for(int n=0;n<64 && ecgPlot.pop(column);++n) {
    int x=column.bin%SCREEN_W;
    bool adjacent=ecgPlotConnected && column.bin-previousEcgBin==1;
    if(ecgPlotConnected && column.bin-previousEcgBin>1) {
      uint32_t distance=column.bin-previousEcgBin;
      if(distance>=SCREEN_W) drawGraphGrid(top,bottom);
      else for(uint32_t i=1;i<distance;++i) eraseEcgPlotColumn((previousEcgBin+i)%SCREEN_W);
    }
    eraseEcgPlotColumn(x); eraseEcgPlotColumn((x+1)%SCREEN_W);
    if(column.valid) {
      float scale=(1<<ecgGain)*0.1f;
      int y1=constrain(int((top+bottom)/2-column.high*scale),top+1,bottom-2);
      int y2=constrain(int((top+bottom)/2-column.low*scale),top+1,bottom-2);
      int mid=(y1+y2)/2;
      if(adjacent && x) tft.drawLine(x-1,prevEcgY,x,mid,COL_ECG_LINE);
      tft.drawFastVLine(x,y1,max(1,y2-y1+1),COL_ECG_LINE); prevEcgY=mid;
    }
    previousEcgBin=column.bin; ecgPlotConnected=column.valid;
  }
}
