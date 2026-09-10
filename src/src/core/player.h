#ifndef player_h
#define player_h
#include "options.h"

#if I2S_DOUT!=255 || I2S_INTERNAL
  #include "../audioI2S/AudioEx.h"
#else
  #include "../audioVS1053/audioVS1053Ex.h"
#endif

#ifndef MQTT_BURL_SIZE
  #define MQTT_BURL_SIZE  512
#endif

#ifndef PLQ_SEND_DELAY
	#define PLQ_SEND_DELAY portMAX_DELAY
#endif

/* Player command queue depth (was 5): burst PR_VOL + WebUI/API could fill queue and block sender forever. /
 * Глубина очереди команд плеера: лавина VOL раньше забивала очередь → зависание на portMAX_DELAY. */
#ifndef PLAYER_QUEUE_LENGTH
#define PLAYER_QUEUE_LENGTH 24
#endif

/* Level adopted as the remembered volume when NVS holds a legacy 0 (see Player::init).
 * Mirrors the factory default in Config::setDefaults(). */
#define PLAYER_VOLUME_FACTORY_DEFAULT  12

#define PLERR_LN        64
#define SET_PLAY_ERROR(...) {char buff[512 + 64]; sprintf(buff,__VA_ARGS__); setError(buff);}

enum playerRequestType_e : uint8_t { PR_PLAY = 1, PR_STOP = 2, PR_PREV = 3, PR_NEXT = 4, PR_VOL = 5, PR_CHECKSD = 6, PR_VUTONUS = 7, PR_MUTE = 8 };

/* PR_MUTE payload: the one semantic MUTE action, callable from any input source
 * (physical BTN_MUTE, touch/UI, future IR, WebUI/telnet) without knowing audio internals.
 * PR_MUTE payload: единственное семантическое действие MUTE для любого источника ввода. */
enum playerMuteRequest_e : int { PMUTE_OFF = 0, PMUTE_ON = 1, PMUTE_TOGGLE = -1 };
struct playerRequestParams_t
{
  playerRequestType_e type;
  int payload;
};

enum plStatus_e : uint8_t{ PLAYING = 1, STOPPED = 2 };

class Player: public Audio {
  private:
    uint32_t    _volTicks;   /* delayed volume save  */
    bool        _volTimer;   /* delayed volume save  */
    int32_t    _resumeFilePos;
    plStatus_e  _status;
    char        _plError[PLERR_LN];
    bool        _muted = false;   /* runtime-only, not persisted (see save_manager.md) */
    /* -1 = never written yet (boot). Dedup for the [MUTE] amp diagnostic - setOutputPins()
     * runs on every play/stop/mute event, often with an unchanged resulting level. */
    int8_t      _ampLastLevel = -1;
  private:
    void _stop(bool alreadyStopped = false);
    void _play(uint16_t stationId);
    /* The only place that hands a level to the codec - gates config.store.volume through _muted.
     * Единственное место, отдающее уровень в кодек, - фильтрует громкость через _muted. */
    void _applyVolumeToAudio();
    void _applyMuteTransition(bool wasMuted);
    void _applyVolPayload(int payload);
    void _applyMuteRequest(int payload);
  public:
    bool lockOutput = true;
    bool resumeAfterUrl = false;
    uint32_t sd_min, sd_max;
    #ifdef MQTT_ROOT_TOPIC
    char      burl[MQTT_BURL_SIZE];  /* buffer for browseUrl  */
    #endif
  public:
    Player();
    void init();
    void loop();
    void initHeaders(const char *file);
    // E36AUD0A1: emitErrorLog=false for system warnings (##SYS# only) / без ##ERROR# для системных
    void setError(const char *e, bool emitErrorLog = true);
    // E36AUD0A: read-only transport error buffer — not track metadata / только ошибка, не title
    bool hasError() const { return strlen(_plError) > 0; }
    const char* lastError() const { return _plError; }
    void sendCommand(playerRequestParams_t request);
    void resetQueue();
    #ifdef MQTT_ROOT_TOPIC
    void browseUrl();
    #endif
    bool remoteStationName = false;
    plStatus_e status() { return _status; }
    void prev();
    void next();
    void toggle();
    void stepVol(bool up);
    void setVol(uint8_t volume);
    /* Central semantic MUTE entry point - queue-based, safe from any task/callback.
     * Центральная точка входа MUTE - через очередь, безопасна из любой задачи. */
    void requestMute(int request = PMUTE_TOGGLE) { sendCommand({PR_MUTE, request}); }
    bool isMuted() const { return _muted; }
    /* Volume as every observer must see it: 0 while muted, otherwise the remembered level.
     * Keeps "0 == MUTE" true on UI, WebUI, telnet and MQTT without a second state flag.
     * Громкость для всех наблюдателей: 0 в MUTE, иначе запомненный уровень.
     * Держит правило "0 == MUTE" единым для UI, WebUI, telnet и MQTT. */
    uint8_t audibleVolume() const;
    uint8_t volToI2S(uint8_t volume);
    void stopInfo();
    void setOutputPins(bool isPlaying);
    void setResumeFilePos(int32_t pos) { _resumeFilePos = pos; }
};

extern Player player;

extern __attribute__((weak)) void player_on_start_play();
extern __attribute__((weak)) void player_on_stop_play();
extern __attribute__((weak)) void player_on_track_change();
extern __attribute__((weak)) void player_on_station_change();

#endif
