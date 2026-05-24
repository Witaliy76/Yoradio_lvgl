#ifndef config_h
#define config_h
#include "Arduino.h"
#include <Ticker.h>
#include <SPI.h>
#include <LittleFS.h>
#include <EEPROM.h>
//#include "SD.h"
#include "options.h"
#include "save_manager.h"
#include "rtcsupport.h"

/* Emulated EEPROM blob size (NVS). Must be >= EEPROM_START + sizeof(config_t): Arduino-ESP32
 * EEPROM.put skips memcpy entirely when address+sizeof(value) exceeds this → silent no persistence. */
#define EEPROM_SIZE       896
#define EEPROM_START      500
#define EEPROM_START_IR   0
#define EEPROM_START_2    10

//#ifdef WROOM_USED
//  #define WROOM            1
//#else
//  #define WROOM            0
//#endif

#ifndef BUFLEN
  #define BUFLEN            250
#endif
#define PLAYLIST_PATH     "/data/playlist.csv"
#define SSIDS_PATH        "/data/wifi.csv"
#define TMP_PATH          "/data/tmpfile.txt"
#define INDEX_PATH        "/data/index.dat"

#define PLAYLIST_SD_PATH     "/data/playlistsd.csv"
#define INDEX_SD_PATH        "/data/indexsd.dat"

#ifdef DEBUG_V
#define DBGH()       { Serial.printf("[%s:%s:%d] Heap: %d\n", __PRETTY_FUNCTION__, __FILE__, __LINE__, xPortGetFreeHeapSize()); }
#define DBGVB( ... ) { char buf[200]; sprintf( buf, __VA_ARGS__ ) ; Serial.print("[DEBUG]\t"); Serial.println(buf); }
#else
#define DBGVB( ... )
#define DBGH()
#endif
#define BOOTLOG( ... ) { char buf[120]; sprintf( buf, __VA_ARGS__ ) ; Serial.print("##[BOOT]#\t"); Serial.println(buf); }
#define EVERY_MS(x)  static uint32_t tmr; bool flag = millis() - tmr >= (x); if (flag) tmr += (x); if (flag)
#define REAL_PLAYL   getMode()==PM_WEB?PLAYLIST_PATH:PLAYLIST_SD_PATH
#define REAL_INDEX   getMode()==PM_WEB?INDEX_PATH:INDEX_SD_PATH

#define MAX_PLAY_MODE   1
#define WEATHERKEY_LENGTH 58
#define MDNS_LENGTH 24
#define AI_API_KEY_LENGTH 64
#define AI_MODEL_LENGTH 32

#if SDC_CS!=255
  #define USE_SD
#endif

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  #define ESP_ARDUINO_3 1
#endif
#define CONFIG_VERSION  6  // Incremented for AI settings addition

enum playMode_e      : uint8_t  { PM_WEB=0, PM_SDCARD=1 };
enum BitrateFormat { BF_UNCNOWN, BF_MP3, BF_AAC, BF_FLAC, BF_OGG, BF_WAV, BF_VOR, BF_OPU };
enum LLMProvider_e  : uint8_t  { LLM_NONE=0, LLM_DEEPSEEK=1, LLM_OPENAI=2 };

void u8fix(char *src);

struct theme_t {
  uint16_t background;
  uint16_t meta;
  uint16_t metabg;
  uint16_t metafill;
  uint16_t title1;
  uint16_t title2;
  uint16_t digit;
  uint16_t div;
  uint16_t weather;
  uint16_t interpretation;  // AI interpretation color / Цвет AI интерпретации
  uint16_t vumax;
  uint16_t vumin;
  uint16_t clock;
  uint16_t clockbg;
  uint16_t seconds;
  uint16_t dow;
  uint16_t date;
  uint16_t heap;
  uint16_t buffer;
  uint16_t ip;
  uint16_t vol;
  uint16_t rssi;
  uint16_t bitrate;
  uint16_t volbarout;
  uint16_t volbarin;
  uint16_t plcurrent;
  uint16_t plcurrentbg;
  uint16_t plcurrentfill;
  uint16_t playlist[5];
};
struct config_t
{
  uint16_t  config_set; //must be 4262
  uint16_t  version;
  uint8_t   volume;
  int8_t    balance;
  int8_t    trebble;
  int8_t    middle;
  int8_t    bass;
  uint16_t  lastStation;
  uint16_t  countStation;
  uint8_t   lastSSID;
  bool      audioinfo;
  uint8_t   smartstart;
  int8_t    tzHour;
  int8_t    tzMin;
  uint16_t  timezoneOffset;
  bool      vumeter;
  uint8_t   softapdelay;
  bool      flipscreen;
  bool      invertdisplay;
  bool      numplaylist;
  bool      fliptouch;
  bool      dbgtouch;
  bool      dspon;
  uint8_t   brightness;
  uint8_t   contrast;
  char      sntp1[35];
  char      sntp2[35];
  bool      showweather;
  char      weatherlat[10];
  char      weatherlon[10];
  char      weatherkey[WEATHERKEY_LENGTH];
  uint16_t  _reserved;
  uint16_t  lastSdStation;
  bool      sdsnuffle;
  uint8_t   volsteps;
  uint16_t  encacc;
  uint8_t   play_mode;  //0 WEB, 1 SD
  uint8_t   irtlp;
  bool      btnpullup;
  uint16_t  btnlongpress;
  uint16_t  btnclickticks;
  uint16_t  btnpressticks;
  bool      encpullup;
  bool      enchalf;
  bool      enc2pullup;
  bool      enc2half;
  bool      forcemono;
  bool      i2sinternal;
  bool      rotate90;
  bool      screensaverEnabled;
  uint16_t  screensaverTimeout;
  bool      screensaverBlank;
  bool      screensaverPlayingEnabled;
  uint16_t  screensaverPlayingTimeout;
  bool      screensaverPlayingBlank;
  char      mdnsname[24];
  bool      skipPlaylistUpDown;
  bool      usespectrum;
  // AI settings / Настройки AI
  bool      ai_enabled;                    // AI включён / AI enabled
  uint8_t   llm_provider;                  // LLM провайдер (LLMProvider_e) / LLM provider
  char      ai_api_key[AI_API_KEY_LENGTH]; // API ключ / API key
  char      ai_model[AI_MODEL_LENGTH];     // Модель / Model
  bool      ai_enableFiles;                // Опция для файлов (если используется) / Files option
};

#if __cplusplus >= 201103L
static_assert(EEPROM_START + sizeof(config_t) <= EEPROM_SIZE,
              "EEPROM_SIZE too small: EEPROM.put(EEPROM_START, config_t) is a no-op on ESP32");
#endif

#include "save_manager_sections.h"

#if IR_PIN!=255
struct ircodes_t
{
  unsigned int ir_set; //must be 4224
  uint64_t irVals[20][3];
};
#endif

struct station_t
{
  char name[BUFLEN];
  char url[BUFLEN];
  char title[BUFLEN];
  uint16_t bitrate;
  int  ovol;
  // Runtime-only stream facts (not in config_t / EEPROM): filled from audio_info() after decode.
  // Рантайм-метаданные текущего потока — не в EEPROM.
  uint32_t stream_sample_rate_hz;  // 0 = unknown for current stream
  uint8_t  stream_bits_per_sample; // 0 = unknown for current stream
};

struct neworkItem
{
  char ssid[30];
  char password[40];
};

class Config {
  public:
    config_t store;
    station_t station;
    theme_t   theme;
#if IR_PIN!=255
    int irindex;
    uint8_t irchck;
    ircodes_t ircodes;
#endif
    BitrateFormat configFmt = BF_UNCNOWN;
    neworkItem ssids[5];
    uint8_t ssidsCount;
    uint16_t sleepfor;
    uint32_t sdResumePos;
    bool     emptyFS;
    uint16_t vuThreshold;
    uint16_t screensaverTicks;
    uint16_t screensaverPlayingTicks;
    bool     isScreensaver;
  public:
    Config() {};
    //void save();
#if IR_PIN!=255
    void saveIR();
#endif
    void init();
    void loadTheme();
    uint8_t setVolume(uint8_t val);
    void saveVolume();
    void setTone(int8_t bass, int8_t middle, int8_t trebble);
    void setBalance(int8_t balance);
    uint8_t setLastStation(uint16_t val);
    uint8_t setCountStation(uint16_t val);
    uint8_t setLastSSID(uint8_t val);
    void setTitle(const char* title);
    void setStation(const char* station);
    bool parseCSV(const char* line, char* name, char* url, int &ovol);
    bool parseJSON(const char* line, char* name, char* url, int &ovol);
    bool parseWsCommand(const char* line, char* cmd, char* val, uint8_t cSize);
    bool parseSsid(const char* line, char* ssid, char* pass);
    void loadStation(uint16_t station);
    bool initNetwork();
    bool saveWifi();
    bool saveWifiFromNextion(const char* post);
    void setSmartStart(uint8_t ss);
    void setBitrateFormat(BitrateFormat fmt) { configFmt = fmt; }
    void initPlaylist();
    void indexPlaylist();
    #ifdef USE_SD
      void initSDPlaylist();
      void changeMode(int newmode=-1);
    #endif
    uint16_t lastStation(){
      return getMode()==PM_WEB?store.lastStation:store.lastSdStation;
    }
    void lastStation(uint16_t newstation){
      if(getMode()==PM_WEB) saveValue(&store.lastStation, newstation);
      else saveValue(&store.lastSdStation, newstation);
    }
    uint8_t fillPlMenu(int from, uint8_t count, bool fromNextion=false);
    char * stationByNum(uint16_t num);
    void setTimezone(int8_t tzh, int8_t tzm);
    void setTimezoneOffset(uint16_t tzo);
    uint16_t getTimezoneOffset();
    void setBrightness(bool dosave=false);
    void setDspOn(bool dspon, bool saveval = true);
    void sleepForAfter(uint16_t sleepfor, uint16_t sleepafter=0);
    void bootInfo();
    void doSleepW();
    void setSnuffle(bool sn);
    uint8_t getMode() { return store.play_mode/* & 0b11*/; }
    void initPlaylistMode();
    void reset();
    bool fsCleanup();
    FS* SDPLFS(){ return _SDplaylistFS; }
    #if RTCSUPPORTED
      bool isRTCFound(){ return _rtcFound; };
    #endif
    template <typename T>
    size_t getAddr(const T *field) const {
      return (size_t)((const uint8_t *)field - (const uint8_t *)&store) + EEPROM_START;
    }
    template <typename T>
    void saveValue(T *field, const T &value, bool commit=true, bool force=false){
      // If this field is unchanged, still notify SaveManager when commit=true so a pending flush
      // from an earlier commit=false write in the same pair cannot be skipped (legacy footgun).
      // Commit-only nudges keep using the v1 entry point — they carry no field identity and must
      // conservatively arm the debouncer for whatever is already dirty (v2 mask or v1 `s_dirty`).
      if(*field == value && !force) {
        if (commit) {
          sm::onStoreWriteCompleted(true);
        }
        return;
      }
      *field = value;
#if SM_V2_ENABLED
      // M3: route through the field-aware path so HOT+META writes go to per-section
      // blobs and cold sections keep falling back to the legacy v1 writer.
      sm::onFieldWrittenV2(field, sizeof(*field), commit);
#else
      sm::onStoreWriteCompleted(commit);
#endif
    }
    void saveValue(char *field, const char *value, size_t N, bool commit=true, bool force=false) {
      if (strcmp(field, value) == 0 && !force) {
        if (commit) {
          sm::onStoreWriteCompleted(true);
        }
        return;
      }
      strlcpy(field, value, N);
#if SM_V2_ENABLED
      // Use `N` (declared buffer length), not strlen — sections are identified by
      // offset, and the bounds check in onFieldWrittenV2 validates against the
      // field's declared footprint.
      sm::onFieldWrittenV2(field, N, commit);
#else
      sm::onStoreWriteCompleted(commit);
#endif
    }
    uint32_t getChipId(){
      uint32_t chipId = 0;
      for(int i=0; i<17; i=i+8) {
        chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
      }
      return chipId;
    }
  private:
    template <class T> int eepromRead(int ee, T& value);
    bool _bootDone;
    #if RTCSUPPORTED
      bool _rtcFound;
    #endif
    FS* _SDplaylistFS;
    void setDefaults();
    Ticker   _sleepTimer;
    static void doSleep();
    uint16_t color565(uint8_t r, uint8_t g, uint8_t b);
    void _setupVersion();
    void _initHW();
    bool _isFSempty();
    uint16_t _randomStation(){
      randomSeed(esp_random() ^ millis());
      uint16_t station = random(1, store.countStation);
      return station;
    }
    char _stationBuf[BUFLEN/2];
};

extern Config config;
#if DSP_HSPI || TS_HSPI || VS_HSPI
extern SPIClass  SPI2;
#endif

// AI configuration structure for filesystem storage / Структура конфигурации AI для хранения в FS
struct AIConfig {
  bool enabled;
  char host[64];
  uint16_t port;
  char path[128];
  uint32_t timeout_ms;
  char api_key[64];
  char model[32];
};

// Get runtime AI configuration from cache (RAM, no FS access) / Получить runtime AI конфигурацию из кеша (RAM, без доступа к FS)
// Returns true if cache is loaded, false if using defaults / Возвращает true если кеш загружен, false если используются дефолты
bool aiGetRuntimeConfig(AIConfig& out);

// Check if filesystem is ready (mounted successfully) / Проверить готовность FS (успешно смонтирована)
bool fsIsReady();

#endif
