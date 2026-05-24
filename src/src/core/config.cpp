#include "config.h"

//#include <LittleFS.h>  // Migrated to LittleFS (Stage 1)
#include "display.h"
#include "player.h"
#include "network.h"
#include "netserver.h"
#include "../ai/ai_log.h"  // AI Layer logging macros
#ifdef USE_SD
#include "sdmanager.h"
#endif
#include <cstddef>
// Runtime AI config cache removed - using filesystem /ai.json as single source of truth
// Runtime AI config кеш удалён - используем FS /ai.json как единственный источник истины
#include <ArduinoJson.h>

Config config;

// Filesystem ready flag (set after LittleFS.begin() in Config::init())
static bool g_fs_ready = false;

// Deferred AI widget clear flag / Флаг отложенной очистки AI виджета
// Set to true when AI is disabled during init (before display is ready)
// Устанавливается в true когда AI выключен во время init (до готовности display)
static bool g_aiNeedsClear = false;

// AIConfig structure is now defined in config.h for public access
// Структура AIConfig теперь определена в config.h для публичного доступа

// Runtime AI configuration cache in RAM / Runtime кеш AI конфигурации в RAM
// Populated from FS /ai.json on startup and after each save
// Заполняется из FS /ai.json при старте и после каждого сохранения
static AIConfig g_ai_cfg;
static bool g_ai_cfg_loaded = false;

// AI configuration file path / Путь к файлу конфигурации AI
#define AI_CONFIG_PATH "/ai.json"

// Set AI configuration defaults / Установить значения по умолчанию для AI конфигурации
void aiSetDefaults(AIConfig& cfg) {
  cfg.enabled = false;
  strlcpy(cfg.host, "api.deepseek.com", sizeof(cfg.host));
  cfg.port = 443;
  strlcpy(cfg.path, "/v1", sizeof(cfg.path));
  cfg.timeout_ms = 6000;
  cfg.api_key[0] = '\0';
  strlcpy(cfg.model, "deepseek-chat", sizeof(cfg.model));
}

// Check if AI configuration is valid for enabling / Проверить валидность AI конфигурации для включения
bool aiIsValidForEnable(const AIConfig& cfg) {
  // Проверяем базовые параметры / Check basic parameters
  if (strlen(cfg.host) == 0 || cfg.port == 0 || strlen(cfg.path) == 0 || 
      strlen(cfg.api_key) == 0 || strlen(cfg.model) == 0) {
    return false;
  }
  
  // СТРОГАЯ ПРОВЕРКА: промпт должен быть доступен / STRICT CHECK: prompt must be available
  extern bool aiPromptIsAvailable();
  if (!aiPromptIsAvailable()) {
    return false;
  }
  
  return true;
}

// Load AI configuration from filesystem / Загрузить конфигурацию AI из FS
// Also updates runtime cache (g_ai_cfg) / Также обновляет runtime кеш (g_ai_cfg)
bool aiLoadFromFS(AIConfig& out) {
  if (!LittleFS.exists(AI_CONFIG_PATH)) {
    aiSetDefaults(out);
    // Update cache with defaults / Обновляем кеш дефолтами
    g_ai_cfg = out;
    g_ai_cfg_loaded = true;
    return false;  // File doesn't exist, using defaults
  }
  
  File file = LittleFS.open(AI_CONFIG_PATH, "r");
  if (!file || file.isDirectory()) {
    aiSetDefaults(out);
    // Update cache with defaults / Обновляем кеш дефолтами
    g_ai_cfg = out;
    g_ai_cfg_loaded = true;
    return false;
  }
  
  size_t size = file.size();
  if (size > 1024) {
    file.close();
    aiSetDefaults(out);
    // Update cache with defaults / Обновляем кеш дефолтами
    g_ai_cfg = out;
    g_ai_cfg_loaded = true;
    return false;  // File too large
  }
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    aiSetDefaults(out);
    // Update cache with defaults / Обновляем кеш дефолтами
    g_ai_cfg = out;
    g_ai_cfg_loaded = true;
    return false;  // JSON parse error
  }
  
  out.enabled = doc["enabled"] | false;
  strlcpy(out.host, doc["host"] | "api.deepseek.com", sizeof(out.host));
  out.port = doc["port"] | 443;
  strlcpy(out.path, doc["path"] | "/v1", sizeof(out.path));
  out.timeout_ms = doc["timeout_ms"] | 6000;
  strlcpy(out.api_key, doc["api_key"] | "", sizeof(out.api_key));
  strlcpy(out.model, doc["model"] | "deepseek-chat", sizeof(out.model));
  
  // Update runtime cache / Обновляем runtime кеш
  g_ai_cfg = out;
  g_ai_cfg_loaded = true;
  
  return true;
}

// Save AI configuration to filesystem / Сохранить конфигурацию AI в FS
// Also updates runtime cache (g_ai_cfg) / Также обновляет runtime кеш (g_ai_cfg)
bool aiSaveToFS(const AIConfig& cfg) {
  File file = LittleFS.open(AI_CONFIG_PATH, "w");
  if (!file) {
    return false;
  }
  
  DynamicJsonDocument doc(1024);
  doc["enabled"] = cfg.enabled;
  doc["host"] = cfg.host;
  doc["port"] = cfg.port;
  doc["path"] = cfg.path;
  doc["timeout_ms"] = cfg.timeout_ms;
  doc["api_key"] = cfg.api_key;
  doc["model"] = cfg.model;
  
  serializeJson(doc, file);
  file.close();
  
  // Update runtime cache after successful save / Обновляем runtime кеш после успешного сохранения
  g_ai_cfg = cfg;
  g_ai_cfg_loaded = true;
  
  return true;
}

// Apply AI configuration to runtime store / Применить конфигурацию AI к runtime store
void aiApplyToStore(const AIConfig& cfg) {
  // Validate before applying / Валидация перед применением
  bool should_enable = cfg.enabled && aiIsValidForEnable(cfg);
  
  // STRICT MODE: если пытались включить, но промпт недоступен - логируем / STRICT MODE: if tried to enable but prompt unavailable - log
  if (cfg.enabled && !should_enable) {
    extern bool aiPromptIsAvailable();
    if (!aiPromptIsAvailable()) {
      AI_LOG("[AI] Enable rejected: prompt missing (/ai/ai_prompt.txt)");
    }
  }
  
  config.saveValue(&config.store.ai_enabled, should_enable, true, true);
  config.saveValue(&config.store.llm_provider, (uint8_t)1, true, true);  // LLM_DEEPSEEK for now
  config.saveValue(config.store.ai_api_key, cfg.api_key, AI_API_KEY_LENGTH, true, true);
  config.saveValue(config.store.ai_model, cfg.model, AI_MODEL_LENGTH, true, true);
  
  // If AI disabled, set deferred clear flag / Если AI выключен, устанавливаем флаг отложенной очистки
  // Don't call display.setAIInterpretation() here - it may be called before display is ready
  // Не вызываем display.setAIInterpretation() здесь - может быть вызван до готовности display
  if (!should_enable) {
    g_aiNeedsClear = true;
  }
}

// Perform deferred AI widget clear if needed / Выполнить отложенную очистку AI виджета если нужно
// Call this after display widgets are initialized (after _buildPager())
// Вызывать после инициализации виджетов display (после _buildPager())
void aiPerformDeferredClearIfNeeded() {
  if (g_aiNeedsClear) {
    display.setAIInterpretation("");
    g_aiNeedsClear = false;
  }
}

void u8fix(char *src){
  char last = src[strlen(src)-1]; 
  if ((uint8_t)last >= 0xC2) src[strlen(src)-1]='\0';
}

bool Config::_isFSempty() {
  const char* reqiredFiles[] = {"index.html","script.js","style.css","settings.html",
                                "settings.css","update.html","ir.html","ir.js",
                                "ir.css","dragpl.js"};
  const uint8_t reqiredFilesSize = 10;
  char fullpath[28];
  for (uint8_t i=0; i<reqiredFilesSize; i++){
    sprintf(fullpath, "/www/%s", reqiredFiles[i]);
    if(!LittleFS.exists(fullpath)) return true;
  }
  return false;
}

// Get runtime AI configuration from cache (RAM, no FS access)
// Получить runtime AI конфигурацию из кеша (RAM, без доступа к FS)
// Returns true if cache is loaded, false if using defaults
// Возвращает true если кеш загружен, false если используются дефолты
bool aiGetRuntimeConfig(AIConfig& out) {
  if (g_ai_cfg_loaded) {
    out = g_ai_cfg;
    return true;
  } else {
    // Cache not loaded yet - return defaults / Кеш ещё не загружен - возвращаем дефолты
    aiSetDefaults(out);
    return false;
  }
}

void Config::init() {
  EEPROM.begin(EEPROM_SIZE);
  sm::init();
  sdResumePos = 0;
  screensaverTicks = 0;
  screensaverPlayingTicks = 0;
  isScreensaver = false;
  bootInfo();
#if RTCSUPPORTED
  _rtcFound = false;
  BOOTLOG("RTC begin(SDA=%d,SCL=%d)", RTC_SDA, RTC_SCL);
  if(rtc.init()){
    BOOTLOG("done");
    _rtcFound = true;
  }else{
    BOOTLOG("[ERROR] - Couldn't find RTC");
  }
#endif
  emptyFS = true;
#if IR_PIN!=255
    irindex=-1;
#endif
// SD SPI инициализация теперь выполняется в sdmanager.cpp
  eepromRead(EEPROM_START, store);
  
  if (store.config_set != 4262) {
    setDefaults();
  }
#if SM_V2_ENABLED
  // v2 overlay after legacy EEPROM read (M5: legacy is read/migration only at runtime;
  // authoritative state for config_t is v2 blobs when the marker is present).
  // Runs after the legacy EEPROM read and the magic/setDefaults branch so that:
  //  - when the marker is present, v2 wins for all managed sections while EEPROM
  //    still supplies the migration snapshot and downgrade window;
  //  - when the marker is absent and legacy is valid, it seeds all v2 blobs +
  //    marker so future boots can overlay;
  //  - when legacy was invalid and setDefaults ran, sm::syncFullStoreNow (called
  //    from setDefaults) already populated v2 blobs + marker; this call is a
  //    no-op overlay from those very blobs onto the defaults-filled store.
  // The version migration `_setupVersion()` below therefore observes the
  // authoritative `store.version` (carried inside the META section).
  sm::v2::runBootMigrationIfNeeded();
#endif
  if(store.version>CONFIG_VERSION) store.version=1;
  while(store.version!=CONFIG_VERSION) _setupVersion();
  BOOTLOG("CONFIG_VERSION\t%d", store.version);
  
  // Проверка и инициализация AI полей если они невалидны (для конфигов которые были обновлены без миграции)
  // Check and initialize AI fields if they are invalid (for configs that were updated without migration)
  // Проверяем llm_provider - если невалидное значение (255 или > LLM_OPENAI), инициализируем все AI поля
  // Check llm_provider - if invalid value (255 or > LLM_OPENAI), initialize all AI fields
  if (store.llm_provider > LLM_OPENAI || store.llm_provider == 255) {
    BOOTLOG("Initializing AI fields (invalid llm_provider=%d)", store.llm_provider);
    saveValue(&store.ai_enabled, false, true, true);  // force=true чтобы перезаписать даже если значение совпадает
    saveValue(&store.llm_provider, (uint8_t)LLM_NONE, true, true);
    saveValue(store.ai_api_key, "", AI_API_KEY_LENGTH, true, true);
    saveValue(store.ai_model, "deepseek-chat", AI_MODEL_LENGTH, true, true);
    saveValue(&store.ai_enableFiles, false, true, true);
  }
  
  // AI runtime config removed - using FS /ai.json as single source of truth
  // Runtime config удалён - используем FS /ai.json как единственный источник истины
  // FS config is loaded later in init() after LittleFS.begin()
  
  #ifdef AI_ENABLE_FILES
    bool files_enabled = (AI_ENABLE_FILES != 0);
    if (store.ai_enableFiles != files_enabled) {
      BOOTLOG("AI enableFiles set to %d from myoptions.h", files_enabled);
      saveValue(&store.ai_enableFiles, files_enabled, true, true);
    }
  #endif
//  if(store.play_mode==80) store.play_mode=0b100;			//**********************************
  store.play_mode = store.play_mode & 0b11;
  if(store.play_mode>1) store.play_mode=PM_WEB;
  _initHW();
  // Stage 1: migrated from SPIFFS to LittleFS. First boot after migration
  // will auto-format (losing old SPIFFS data). Re-upload via `pio run -t uploadfs`.
  g_fs_ready = LittleFS.begin(true);
  if (!g_fs_ready) {
    Serial.println("##[ERROR]#\tLittleFS Mount Failed");
    return;
  }
  BOOTLOG("LittleFS mounted");
  LittleFS.mkdir("/data");
  LittleFS.mkdir("/www");
  LittleFS.mkdir("/ai");
  LittleFS.mkdir("/bg");
  LittleFS.mkdir("/logo"); // Station Art MVP: per-station art files /logo/<normalized_name>.bin
  
  // Load AI configuration from filesystem and apply to store
  // FS config has priority over runtime config (WebUI settings override dev config)
  // IMPORTANT: Must be called AFTER LittleFS.begin() / ВАЖНО: Вызывать ПОСЛЕ LittleFS.begin()
  if (g_fs_ready) {
    // Reset AI prompt cache after FS is ready
    // This allows prompts to be loaded from files on first request / Это позволяет загрузить промпты из файлов при первом запросе
    extern void aiPromptResetCache();
    aiPromptResetCache();
    
    // Check prompt availability and log / Проверка наличия промпта и логирование
    extern bool aiPromptIsAvailable();
    if (!aiPromptIsAvailable()) {
      BOOTLOG("[AI] Prompt missing: /ai/ai_prompt.txt");
      BOOTLOG("[AI] AI disabled until prompt is uploaded to filesystem");
      BOOTLOG("[AI] Please upload: /ai/ai_prompt.txt");
    }
    
    AIConfig aicfg;
    if (aiLoadFromFS(aicfg)) {
      // File exists, apply it / Файл существует, применяем его
      // STRICT MODE: If enabled but prompt missing, force disable / СТРОГИЙ РЕЖИМ: Если включён но промпт отсутствует, принудительно выключаем
      if (aicfg.enabled && !aiPromptIsAvailable()) {
        aicfg.enabled = false;
        aiSaveToFS(aicfg);  // Сохраняем исправленное состояние / Save corrected state
        BOOTLOG("[AI] Enable rejected: prompt missing (/ai/ai_prompt.txt) - corrected /ai.json");
      }
      aiApplyToStore(aicfg);
    }
  }
  emptyFS = _isFSempty();
  if(emptyFS) BOOTLOG("LittleFS is empty!");
  ssidsCount = 0;
  #ifdef USE_SD
  _SDplaylistFS = getMode()==PM_SDCARD?&sdman:(true?&LittleFS:_SDplaylistFS);
  #else
  _SDplaylistFS = &LittleFS;
  #endif
  _bootDone=false;
}

void Config::_setupVersion(){
  uint16_t currentVersion = store.version;
  switch(currentVersion){
    case 1:
      saveValue(&store.screensaverEnabled, false);
      saveValue(&store.screensaverTimeout, (uint16_t)20);
      break;
    case 2:
      char buf[MDNS_LENGTH];
      snprintf(buf, MDNS_LENGTH, "yoradio-%x", getChipId());
      saveValue(store.mdnsname, buf, MDNS_LENGTH);
      saveValue(&store.skipPlaylistUpDown, false);
      break;
    case 3:
      saveValue(&store.screensaverBlank, false);
      saveValue(&store.screensaverPlayingEnabled, false);
      saveValue(&store.screensaverPlayingTimeout, (uint16_t)20);
      saveValue(&store.screensaverPlayingBlank, false);
      break;
    case 4:
      // introduce usespectrum with default false
      saveValue(&store.usespectrum, false);
      break;
    case 5:
      // introduce AI settings with defaults
      saveValue(&store.ai_enabled, false);
      saveValue(&store.llm_provider, (uint8_t)LLM_NONE);
      saveValue(store.ai_api_key, "", AI_API_KEY_LENGTH);
      saveValue(store.ai_model, "deepseek-chat", AI_MODEL_LENGTH);
      saveValue(&store.ai_enableFiles, false);
      break;
    default:
      break;
  }
  currentVersion++;
  saveValue(&store.version, currentVersion);
}

#ifdef USE_SD

void Config::changeMode(int newmode){
  bool pir = player.isRunning();
  if(SDC_CS==255) return;
  if(getMode()==PM_SDCARD) {
    sdResumePos = player.getFilePos();
  }
  if(network.status==SOFT_AP || display.mode()==LOST){
    saveValue(&store.play_mode, static_cast<uint8_t>(PM_SDCARD));
    delay(50);
    ESP.restart();
  }
  if(!sdman.ready && newmode!=PM_WEB) {
    if(!sdman.start()){
      Serial.println("##[ERROR]#\tSD Not Found");
      netserver.requestOnChange(GETPLAYERMODE, 0);
      sdman.stop();
      return;
    }
  }
  if(newmode<0){
    store.play_mode++;
    if(getMode() > MAX_PLAY_MODE) store.play_mode=0;
  }else{
    store.play_mode=(playMode_e)newmode;
  }
  saveValue(&store.play_mode, store.play_mode, true, true);
  _SDplaylistFS = getMode()==PM_SDCARD?&sdman:(true?&LittleFS:_SDplaylistFS);
  if(getMode()==PM_SDCARD){
    if(pir) player.sendCommand({PR_STOP, 0});
    // Block 8-E8: no SDCHANGE UI wait — sdman.start() above; playlist via initPlaylistMode().
  }
  if(getMode()==PM_WEB) {
    if(network.status==SDREADY) ESP.restart();
    sdman.stop();
  }
  if(!_bootDone) return;
  initPlaylistMode();
//  if ((pir) && (store.smartstart == 1)) player.sendCommand({PR_PLAY, getMode()==PM_WEB?store.lastStation:store.lastSdStation});
  if (pir) player.sendCommand({PR_PLAY, getMode()==PM_WEB?store.lastStation:store.lastSdStation});
  netserver.resetQueue();
  netserver.requestOnChange(GETPLAYERMODE, 0);
  netserver.requestOnChange(GETMODE, 0);
  display.resetQueue();
  display.putRequest(NEWMODE, PLAYER);
  display.putRequest(NEWSTATION);
}

void Config::initSDPlaylist() {
  store.countStation = 0;
  bool indexExists = sdman.exists(INDEX_SD_PATH);
  bool doIndex = !indexExists;
  
  // Если индекс существует, проверяем его размер
  if (indexExists) {
    File testIndex = SDPLFS()->open(INDEX_SD_PATH, "r");
    if (testIndex) {
      size_t indexSize = testIndex.size();
      testIndex.close();
      if (indexSize == 0) {
        doIndex = true;
        sdman.remove(INDEX_SD_PATH);
        sdman.remove(PLAYLIST_SD_PATH);
      }
    }
  }
  
  if(doIndex) {
    sdman.indexSDPlaylist();
  }
  
  if (SDPLFS()->exists(INDEX_SD_PATH)) {
    File index = SDPLFS()->open(INDEX_SD_PATH, "r");
    if (index) {
      store.countStation = index.size() / 4;
      if(doIndex){
        lastStation(_randomStation());
        sdResumePos = 0;
      }
      index.close();
      saveValue(&store.countStation, store.countStation, true, true);
    }
  }
}

#endif //#ifdef USE_SD

bool Config::fsCleanup(){
  bool ret = (LittleFS.exists(PLAYLIST_SD_PATH)) || (LittleFS.exists(INDEX_SD_PATH)) || (LittleFS.exists(INDEX_PATH));
  if(LittleFS.exists(PLAYLIST_SD_PATH)) LittleFS.remove(PLAYLIST_SD_PATH);
  if(LittleFS.exists(INDEX_SD_PATH)) LittleFS.remove(INDEX_SD_PATH);
  if(LittleFS.exists(INDEX_PATH)) LittleFS.remove(INDEX_PATH);
  return ret;
}

void Config::initPlaylistMode(){
  uint16_t _lastStation = 0;
  #ifdef USE_SD
    if(getMode()==PM_SDCARD){
      if(!sdman.start()){
        store.play_mode=PM_WEB;
        Serial.println("SD Mount Failed");
        changeMode(PM_WEB);
        _lastStation = store.lastStation;
      }else{
        if(_bootDone) Serial.println("SD Mounted"); else BOOTLOG("SD Mounted");
          if(_bootDone) Serial.println("Waiting for SD card indexing..."); else BOOTLOG("Waiting for SD card indexing...");
          initSDPlaylist();
          if(_bootDone) Serial.println("done"); else BOOTLOG("done");
          _lastStation = store.lastSdStation;
          if(_lastStation>store.countStation && store.countStation>0){
            _lastStation=1;
          }
          if(_lastStation==0) {
            _lastStation = _randomStation();
          }
      }
    }else{
      Serial.println("done");
      _lastStation = store.lastStation;
    }
  #else //ifdef USE_SD
    store.play_mode=PM_WEB;
    _lastStation = store.lastStation;
  #endif
  if(getMode()==PM_WEB && !emptyFS) initPlaylist();
  log_i("%d" ,_lastStation);
  if (_lastStation == 0 && store.countStation > 0) {
    _lastStation = getMode()==PM_WEB?1:_randomStation();
  }
  lastStation(_lastStation);
  saveValue(&store.play_mode, store.play_mode, true, true);
  _bootDone = true;
  loadStation(_lastStation);
}

void Config::_initHW(){
  loadTheme();
  #if IR_PIN!=255
  eepromRead(EEPROM_START_IR, ircodes);
  if(ircodes.ir_set!=4224){
    ircodes.ir_set=4224;
    memset(ircodes.irVals, 0, sizeof(ircodes.irVals));
  }
  #endif
  #if BRIGHTNESS_PIN!=255
    pinMode(BRIGHTNESS_PIN, OUTPUT);
    setBrightness(false);
  #endif
}

uint16_t Config::color565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void Config::loadTheme(){
  theme.background    = color565(COLOR_BACKGROUND);
  theme.meta          = color565(COLOR_STATION_NAME);
  theme.metabg        = color565(COLOR_STATION_BG);
  theme.metafill      = color565(COLOR_STATION_FILL);
  theme.title1        = color565(COLOR_SNG_TITLE_1);
  theme.title2        = color565(COLOR_SNG_TITLE_2);
  theme.digit         = color565(COLOR_DIGITS);
  theme.div           = color565(COLOR_DIVIDER);
  theme.weather       = color565(COLOR_WEATHER);
  theme.interpretation = color565(COLOR_AI_INTERPRETATION);
  theme.vumax         = color565(COLOR_VU_MAX);
  theme.vumin         = color565(COLOR_VU_MIN);
  theme.clock         = color565(COLOR_CLOCK);
  theme.clockbg       = color565(COLOR_CLOCK_BG);
  theme.seconds       = color565(COLOR_SECONDS);
  theme.dow           = color565(COLOR_DAY_OF_W);
  theme.date          = color565(COLOR_DATE);
  theme.heap          = color565(COLOR_HEAP);
  theme.buffer        = color565(COLOR_BUFFER);
  theme.ip            = color565(COLOR_IP);
  theme.vol           = color565(COLOR_VOLUME_VALUE);
  theme.rssi          = color565(COLOR_RSSI);
  theme.bitrate       = color565(COLOR_BITRATE);
  theme.volbarout     = color565(COLOR_VOLBAR_OUT);
  theme.volbarin      = color565(COLOR_VOLBAR_IN);
  theme.plcurrent     = color565(COLOR_PL_CURRENT);
  theme.plcurrentbg   = color565(COLOR_PL_CURRENT_BG);
  theme.plcurrentfill = color565(COLOR_PL_CURRENT_FILL);
  theme.playlist[0]   = color565(COLOR_PLAYLIST_0);
  theme.playlist[1]   = color565(COLOR_PLAYLIST_1);
  theme.playlist[2]   = color565(COLOR_PLAYLIST_2);
  theme.playlist[3]   = color565(COLOR_PLAYLIST_3);
  theme.playlist[4]   = color565(COLOR_PLAYLIST_4);
  #include "../displays/tools/tftinverttitle.h"
}

template <class T> int Config::eepromRead(int ee, T& value) {
  uint8_t* p = (uint8_t*)(void*)&value;
  int i;;
  for (i = 0; i < sizeof(value); i++)
    *p++ = EEPROM.read(ee++);
  return i;
}

void Config::reset(){
  setDefaults();
  delay(500);
  sm::systemRestart();
}

void Config::setDefaults() {
  store.config_set = 4262;
  store.version = CONFIG_VERSION;
  store.volume = 12;
  store.balance = 0;
  store.trebble = 0;
  store.middle = 0;
  store.bass = 0;
  store.lastStation = 0;
  store.countStation = 0;
  store.lastSSID = 0;
  store.audioinfo = false;
  store.smartstart = 2;
  store.tzHour = 3;
  store.tzMin = 0;
  store.timezoneOffset = 0;

  store.vumeter=false;
  store.softapdelay=0;
  store.flipscreen=false;
  store.invertdisplay=false;
  store.numplaylist=false;
  store.fliptouch=false;
  store.dbgtouch=false;
  store.dspon=true;
  store.brightness=100;
  store.contrast=55;
  strlcpy(store.sntp1,"2.ru.pool.ntp.org", 35);
  strlcpy(store.sntp2,"1.ru.pool.ntp.org", 35);
  store.showweather=false;
  strlcpy(store.weatherlat,"55.7512", 10);
  strlcpy(store.weatherlon,"37.6184", 10);
  strlcpy(store.weatherkey,"", WEATHERKEY_LENGTH);
  store._reserved = 0;
  store.lastSdStation = 0;
  store.sdsnuffle = false;
  store.volsteps = 1;
  store.encacc = 200;
  store.play_mode = 0;
  store.irtlp = 35;
  store.btnpullup = true;
  store.btnlongpress = 200;
  store.btnclickticks = 300;
  store.btnpressticks = 500;
  store.encpullup = false;
  store.enchalf = false;
  store.enc2pullup = false;
  store.enc2half = false;
  store.forcemono = false;
  store.i2sinternal = false;
  store.rotate90 = false;
  store.screensaverEnabled = false;
  store.screensaverTimeout = 20;
  snprintf(store.mdnsname, MDNS_LENGTH, "yoradio-%x", getChipId());
  store.skipPlaylistUpDown = false;
  store.screensaverPlayingEnabled = false;
  store.screensaverPlayingTimeout = 5;
  store.usespectrum = false;
  // AI defaults / Значения по умолчанию для AI
  store.ai_enabled = false;
  store.llm_provider = LLM_NONE;
  strlcpy(store.ai_api_key, "", AI_API_KEY_LENGTH);
  strlcpy(store.ai_model, "deepseek-chat", AI_MODEL_LENGTH);  // DeepSeek default model
  store.ai_enableFiles = false;
  
  // AI settings migrated to FS /ai.json and runtime cache (see aiGetRuntimeConfig())
  // Настройки AI мигрированы на FS /ai.json и runtime кеш (см. aiGetRuntimeConfig())
  // Runtime config will be applied in Config::init() after store is loaded
  // Runtime config будет применена в Config::init() после загрузки store
  
  sm::syncFullStoreNow();
}
void Config::setTimezone(int8_t tzh, int8_t tzm) {
  saveValue(&store.tzHour, tzh, false);
  saveValue(&store.tzMin, tzm);
}

void Config::setTimezoneOffset(uint16_t tzo) {
  saveValue(&store.timezoneOffset, tzo);
}

uint16_t Config::getTimezoneOffset() {
  return 0; // TODO
}

void Config::setSnuffle(bool sn){
  saveValue(&store.sdsnuffle, sn);
  if(store.sdsnuffle) player.next();
}

#if IR_PIN!=255
void Config::saveIR(){
  sm::syncIrBlobNow(&ircodes, sizeof(ircodes), EEPROM_START_IR);
}
#endif

void Config::saveVolume(){
  saveValue(&store.volume, store.volume, true, true);
}

uint8_t Config::setVolume(uint8_t val) {
  saveValue(&store.volume, val);
  display.putRequest(DRAWVOL);
  netserver.requestOnChange(VOLUME, 0);
  return store.volume;
}

void Config::setTone(int8_t bass, int8_t middle, int8_t trebble) {
  saveValue(&store.bass, bass, false);
  saveValue(&store.middle, middle, false);
  saveValue(&store.trebble, trebble);
}

void Config::setSmartStart(uint8_t ss) {
  saveValue(&store.smartstart, ss);
}

void Config::setBalance(int8_t balance) {
  saveValue(&store.balance, balance);
}

uint8_t Config::setLastStation(uint16_t val) {
  lastStation(val);
  return store.lastStation;
}

uint8_t Config::setCountStation(uint16_t val) {
  saveValue(&store.countStation, val);
  return store.countStation;
}

uint8_t Config::setLastSSID(uint8_t val) {
  saveValue(&store.lastSSID, val);
  return store.lastSSID;
}

void Config::setTitle(const char* title) {
  vuThreshold = 0;
  memset(config.station.title, 0, BUFLEN);
  strlcpy(config.station.title, title, BUFLEN);
  u8fix(config.station.title);
  netserver.requestOnChange(TITLE, 0);
  netserver.loop();
  display.putRequest(NEWTITLE);
}

void Config::setStation(const char* station) {
  memset(config.station.name, 0, BUFLEN);
  strlcpy(config.station.name, station, BUFLEN);
  u8fix(config.station.title);
}

void Config::indexPlaylist() {
  File playlist = LittleFS.open(PLAYLIST_PATH, "r");
  if (!playlist) {
    return;
  }
  char sName[BUFLEN], sUrl[BUFLEN];
  int sOvol;
  File index = LittleFS.open(INDEX_PATH, "w");
  while (playlist.available()) {
    uint32_t pos = playlist.position();
    if (parseCSV(playlist.readStringUntil('\n').c_str(), sName, sUrl, sOvol)) {
      index.write((uint8_t *) &pos, 4);
    }
  }
  index.close();
  playlist.close();
}

void Config::initPlaylist() {
  store.countStation = 0;
  if (!LittleFS.exists(INDEX_PATH)) indexPlaylist();

  if (LittleFS.exists(INDEX_PATH)) {
    File index = LittleFS.open(INDEX_PATH, "r");
    store.countStation = index.size() / 4;
    index.close();
    saveValue(&store.countStation, store.countStation, true, true);
  }
}

void Config::loadStation(uint16_t ls) {
  char sName[BUFLEN], sUrl[BUFLEN];
  int sOvol;
  if (store.countStation == 0) {
    memset(station.url, 0, BUFLEN);
    memset(station.name, 0, BUFLEN);
    strncpy(station.name, "ёRadio", BUFLEN);
    station.ovol = 0;
    return;
  }
  if (ls > store.countStation) {
    ls = 1;
  }
  File playlist = SDPLFS()->open(REAL_PLAYL, "r");
  File index = SDPLFS()->open(REAL_INDEX, "r");
  index.seek((ls - 1) * 4, SeekSet);
  uint32_t pos;
  index.readBytes((char *) &pos, 4);
  index.close();
  playlist.seek(pos, SeekSet);
  if (parseCSV(playlist.readStringUntil('\n').c_str(), sName, sUrl, sOvol)) {
    memset(station.url, 0, BUFLEN);
    memset(station.name, 0, BUFLEN);
    strncpy(station.name, sName, BUFLEN);
    strncpy(station.url, sUrl, BUFLEN);
    station.ovol = sOvol;
    setLastStation(ls);
  }
  playlist.close();
}

char * Config::stationByNum(uint16_t num){
  File playlist = SDPLFS()->open(REAL_PLAYL, "r");
  File index = SDPLFS()->open(REAL_INDEX, "r");
  index.seek((num - 1) * 4, SeekSet);
  uint32_t pos;
  memset(_stationBuf, 0, BUFLEN/2);
  index.readBytes((char *) &pos, 4);
  index.close();
  playlist.seek(pos, SeekSet);
  strncpy(_stationBuf, playlist.readStringUntil('\t').c_str(), BUFLEN/2);
  playlist.close();
  return _stationBuf;
}

uint8_t Config::fillPlMenu(int from, uint8_t count, bool fromNextion) {
  int     ls      = from;
  uint8_t c       = 0;
  bool    finded  = false;
  if (store.countStation == 0) {
    return 0;
  }
  File playlist = SDPLFS()->open(REAL_PLAYL, "r");
  File index = SDPLFS()->open(REAL_INDEX, "r");
  while (true) {
    if (ls < 1) {
      ls++;
      display.printPLitem(c, "", playlistConf.uppercase);
      (void)fromNextion;
      c++;
      continue;
    }
    if (!finded) {
      index.seek((ls - 1) * 4, SeekSet);
      uint32_t pos;
      index.readBytes((char *) &pos, 4);
      finded = true;
      index.close();
      playlist.seek(pos, SeekSet);
    }
    bool pla = true;
    while (pla) {
      pla = playlist.available();
      String stationName = playlist.readStringUntil('\n');
      stationName = stationName.substring(0, stationName.indexOf('\t'));
      if(config.store.numplaylist && stationName.length()>0) stationName = String(from+c)+" "+stationName;
      display.printPLitem(c, stationName.c_str(), playlistConf.uppercase);
      (void)fromNextion;
      c++;
      if (c >= count) break;
    }
    break;
  }
  playlist.close();
  return c;
}

bool Config::parseCSV(const char* line, char* name, char* url, int &ovol) {
  char *tmpe;
  const char* cursor = line;
  char buf[5];
  tmpe = strstr(cursor, "\t");
  if (tmpe == NULL) return false;
  strlcpy(name, cursor, tmpe - cursor + 1);
  if (strlen(name) == 0) return false;
  cursor = tmpe + 1;
  tmpe = strstr(cursor, "\t");
  if (tmpe == NULL) return false;
  strlcpy(url, cursor, tmpe - cursor + 1);
  if (strlen(url) == 0) return false;
  cursor = tmpe + 1;
  if (strlen(cursor) == 0) return false;
  strlcpy(buf, cursor, 4);
  ovol = atoi(buf);
  return true;
}

bool Config::parseJSON(const char* line, char* name, char* url, int &ovol) {
  char* tmps, *tmpe;
  const char* cursor = line;
  char port[8], host[246], file[254];
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  strlcpy(name, tmps + 3, tmpe - tmps - 3 + 1);
  if (strlen(name) == 0) return false;
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  strlcpy(host, tmps + 3, tmpe - tmps - 3 + 1);
  if (strlen(host) == 0) return false;
  if (strstr(host, "http://") == NULL && strstr(host, "https://") == NULL) {
    sprintf(file, "http://%s", host);
    strlcpy(host, file, strlen(file) + 1);
  }
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  strlcpy(file, tmps + 3, tmpe - tmps - 3 + 1);
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  strlcpy(port, tmps + 3, tmpe - tmps - 3 + 1);
  int p = atoi(port);
  if (p > 0) {
    sprintf(url, "%s:%d%s", host, p, file);
  } else {
    sprintf(url, "%s%s", host, file);
  }
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\"}");
  if (tmpe == NULL) return false;
  strlcpy(port, tmps + 3, tmpe - tmps - 3 + 1);
  ovol = atoi(port);
  return true;
}

bool Config::parseWsCommand(const char* line, char* cmd, char* val, uint8_t cSize) {
  char *tmpe;
  tmpe = strstr(line, "=");
  if (tmpe == NULL) return false;
  memset(cmd, 0, cSize);
  strlcpy(cmd, line, tmpe - line + 1);
  //if (strlen(tmpe + 1) == 0) return false;
  memset(val, 0, cSize);
  strlcpy(val, tmpe + 1, strlen(line) - strlen(cmd) + 1);
  return true;
}

bool Config::parseSsid(const char* line, char* ssid, char* pass) {
  char *tmpe;
  tmpe = strstr(line, "\t");
  if (tmpe == NULL) return false;
  uint16_t pos = tmpe - line;
  if (pos > 29 || strlen(line) > 71) return false;
  memset(ssid, 0, 30);
  strlcpy(ssid, line, pos + 1);
  memset(pass, 0, 40);
  strlcpy(pass, line + pos + 1, strlen(line) - pos);
  return true;
}

bool Config::saveWifiFromNextion(const char* post){
  File file = LittleFS.open(SSIDS_PATH, "w");
  if (!file) {
    return false;
  } else {
    file.print(post);
    file.close();
    ESP.restart();
    return true;
  }
}

// Check if filesystem is ready (mounted successfully)
bool fsIsReady() {
  return g_fs_ready;
}

bool Config::saveWifi() {
  if (!LittleFS.exists(TMP_PATH)) return false;
  LittleFS.remove(SSIDS_PATH);
  LittleFS.rename(TMP_PATH, SSIDS_PATH);
  ESP.restart();
  return true;
}

bool Config::initNetwork() {
  File file = LittleFS.open(SSIDS_PATH, "r");
  if (!file || file.isDirectory()) {
    return false;
  }
  char ssidval[30], passval[40];
  uint8_t c = 0;
  while (file.available()) {
    if (parseSsid(file.readStringUntil('\n').c_str(), ssidval, passval)) {
      strlcpy(ssids[c].ssid, ssidval, 30);
      strlcpy(ssids[c].password, passval, 40);
      ssidsCount++;
      c++;
      if (c >= 5) {
        break;
      }
    }
  }
  file.close();
  return true;
}

void Config::setBrightness(bool dosave){
#ifdef ENABLE_BRIGHTNESS_CONTROL
  if(!store.dspon && dosave) {
    display.wakeup();
  }
  dsp.setBrightness(store.brightness);
  if(!store.dspon) store.dspon = true;
  if(dosave){
    saveValue(&store.brightness, store.brightness, false, true);
    saveValue(&store.dspon, store.dspon, true, true);
  }
#endif
}

void Config::setDspOn(bool dspon, bool saveval){
  if(saveval){
    store.dspon = dspon;
    saveValue(&store.dspon, store.dspon, true, true);
  }
  if(!dspon){
#if BRIGHTNESS_PIN!=255
  analogWrite(BRIGHTNESS_PIN, 0);
#endif
    display.deepsleep();
  }else{
    display.wakeup();
#if BRIGHTNESS_PIN!=255
  analogWrite(BRIGHTNESS_PIN, map(store.brightness, 0, 100, 0, 255));
#endif
  }
}

void Config::doSleep(){
  if(BRIGHTNESS_PIN!=255) analogWrite(BRIGHTNESS_PIN, 0);
  display.deepsleep();
#if !defined(ARDUINO_ESP32C3_DEV)
  if(WAKE_PIN!=255) esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_PIN, LOW);
  esp_sleep_enable_timer_wakeup(config.sleepfor * 60 * 1000000ULL);
  esp_deep_sleep_start();
#endif
}

void Config::doSleepW(){
  if(BRIGHTNESS_PIN!=255) analogWrite(BRIGHTNESS_PIN, 0);
  display.deepsleep();
#if !defined(ARDUINO_ESP32C3_DEV)
  if(WAKE_PIN!=255) esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_PIN, LOW);
  esp_deep_sleep_start();
#endif
}

void Config::sleepForAfter(uint16_t sf, uint16_t sa){
  sleepfor = sf;
  if(sa > 0) _sleepTimer.attach(sa * 60, doSleep);
  else doSleep();
}

void Config::bootInfo() {
  BOOTLOG("************************************************");
  BOOTLOG("*               ёRadio v%s             *", YOVERSION);
  BOOTLOG("************************************************");
  BOOTLOG("------------------------------------------------");
  BOOTLOG("arduino:\t%d", ARDUINO);
  BOOTLOG("compiler:\t%s", __VERSION__);
  BOOTLOG("esp32core:\t%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  uint32_t chipId = 0;
  for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  BOOTLOG("chip:\t\tmodel: %s | rev: %d | id: %d | cores: %d | psram: %d", ESP.getChipModel(), ESP.getChipRevision(), chipId, ESP.getChipCores(), ESP.getPsramSize());
  BOOTLOG("display:\tmodel: %d (CS-%d, DC-%d, RST-%d, SPI-%s)", DSP_MODEL, TFT_CS, TFT_DC, TFT_RST, DSP_HSPI?"HSPI":"VSPI");
//  BOOTLOG("display:\tmodel: %d, (CS-%d, DC-%d, RST-%d, SPI-%s, MOSI-%d, SCLK-%d)", DSP_MODEL, TFT_CS, TFT_DC, TFT_RST, DSP_HSPI?"HSPI":"VSPI", mosi_pin, sck_pin);
  if(VS1053_CS==255) {
    BOOTLOG("audio:\t\t%s (DOUT-%d, BCLK-%d, LRC-%d)", "I2S", I2S_DOUT, I2S_BCLK, I2S_LRC);
  }else{
    #ifdef ARDUINO_ESP32S3_DEV
        BOOTLOG("audio:\t\t%s (CS-%d, DCS-%d, DREQ-%d, RST-%d, SPI-%s)", "VS1053", VS1053_CS, VS1053_DCS, VS1053_DREQ, VS1053_RST, VS_SSPI?"SubSPI":"FSPI");
//        BOOTLOG("audio:\t\t%s (CS-%d, DCS-%d, DREQ-%d, RST-%d, SPI-%s, MOSI-%d, MISO-%d, SCK-%d)", "VS1053", VS1053_CS, VS1053_DCS, VS1053_DREQ, VS1053_RST, VS_SSPI?"SubSPI":"FSPI", mosi_pin, miso_pin, sclk_pin);
    #else
        BOOTLOG("audio:\t\t%s (CS-%d, DCS-%d, DREQ-%d, RST-%d, SPI-%s)", "VS1053", VS1053_CS, VS1053_DCS, VS1053_DREQ, VS1053_RST, VS_HSPI?"HSPI":"VSPI");
//        BOOTLOG("audio:\t\t%s (CS-%d, DCS-%d, DREQ-%d, RST-%d, SPI-%s, MOSI-%d, MISO-%d, SCK-%d)", "VS1053", VS1053_CS, VS1053_DCS, VS1053_DREQ, VS1053_RST, VS_HSPI?"HSPI":"VSPI", mosi_pin, miso_pin, sclk_pin);
    #endif
  }
  BOOTLOG("audioinfo:\t%s", store.audioinfo?"true":"false");
  BOOTLOG("smartstart:\t%d", store.smartstart);
  BOOTLOG("vumeter:\t%s", store.vumeter?"true":"false");
  BOOTLOG("softapdelay:\t%d", store.softapdelay);
  BOOTLOG("flipscreen:\t%s", store.flipscreen?"true":"false");
  BOOTLOG("invertdisplay:\t%s", store.invertdisplay?"true":"false");
  BOOTLOG("showweather:\t%s", store.showweather?"true":"false");
  BOOTLOG("buttons:\tleft=%d, center=%d, right=%d, up=%d, down=%d, mode=%d, pullup=%s", BTN_LEFT, BTN_CENTER, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_MODE, BTN_INTERNALPULLUP?"ON":"OFF");
  BOOTLOG("encoders:\tl1=%d, b1=%d, r1=%d, pullup=%s, l2=%d, b2=%d, r2=%d, pullup=%s", ENC_BTNL, ENC_BTNB, ENC_BTNR, ENC_INTERNALPULLUP?"ON":"OFF", ENC2_BTNL, ENC2_BTNB, ENC2_BTNR, ENC2_INTERNALPULLUP?"ON":"OFF");
  if(IR_PIN!=255) {BOOTLOG("ir:\t\t%d", IR_PIN);}else{BOOTLOG("ir:\t\tNONE");}
  if(SDC_CS!=255) {BOOTLOG("SD:\t\tCS-%d", SDC_CS);}else{BOOTLOG("SD:\t\tNONE");}
    #ifdef BATTERY_OFF
  BOOTLOG("battery:\t%s", "battery OFF");
    #else
  BOOTLOG("battery:\t%s", "battery ON");
    #endif
  BOOTLOG("------------------------------------------------");
}
