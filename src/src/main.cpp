#include "Arduino.h"
#include "core/options.h"
#include "core/config.h"
#include "core/telnet.h"
#include "core/player.h"
#include "core/display.h"
#include "core/network.h"
#include "core/netserver.h"
#include "core/controls.h"
#include "core/mqtt.h"
#include "core/optionschecker.h"
#include "core/mem_watchdog.h"

// AI subsystem (Stage 6.0) / AI-подсистема
#include "ai/ai_subsystem.h"

#if DSP_HSPI || TS_HSPI || VS_HSPI
SPIClass  SPI2(HOOPSENb);
#endif

extern __attribute__((weak)) void yoradio_on_setup();

void setup() {
  Serial.begin(115200);
  if(REAL_LEDBUILTIN!=255) pinMode(REAL_LEDBUILTIN, OUTPUT);
  if (yoradio_on_setup) yoradio_on_setup();
  
  // AI subsystem: explicit init before config (same order as former AIPlugin path)
  // AI-подсистема: явный init до config (тот же порядок, что у AIPlugin)
  aiSubsystem.init();
  aiSubsystem.onSetup();
  config.init();
#ifdef MEM_WATCHDOG_AUTOREBOOT
  memWatchdog.onBoot();
#endif
  display.init();

  // Block 8-E18D: SpectrumAnalyzer runtime disabled (see audio_process_i2s).
  // Block 8-E18D: SpectrumAnalyzer отключён в runtime (см. audio_process_i2s).

  player.init();
  network.begin();
  if (network.status != CONNECTED && network.status!=SDREADY) {
    // E36FS1: FAILED (no creds / S6V9C) — no NetServer until STA or Hotspot AP has IP.
    // E36FS1: FAILED — NetServer только после IP (STA или Hotspot AP).
    if (network.status == SOFT_AP) {
      netserver.begin();
    }
    initControls();
    Serial.println("[Main] Sending DSP_START request");
    display.putRequest(DSP_START);
    Serial.println("[Main] Waiting for display.ready()");
    int waitCount = 0;
    while(!display.ready()) {
      delay(10);
      waitCount++;
      if(waitCount % 100 == 0) {
        Serial.printf("[Main] Still waiting for display.ready(), count=%d\n", waitCount);
      }
    }
    Serial.println("[Main] Display is ready, returning");
    return;
  }
  if(SDC_CS!=255) {
    display.putRequest(WAITFORSD, 0);
    Serial.print("##[BOOT]#\tSD search\t");
  }
  config.initPlaylistMode();
  netserver.begin();
  telnet.begin();
  initControls();
  display.putRequest(DSP_START);
  while(!display.ready()) delay(10);
  #ifdef MQTT_ROOT_TOPIC
    mqttInit();
  #endif
  if (config.getMode()==PM_SDCARD) player.initHeaders(config.station.url);
  player.lockOutput=false;
  if (config.store.smartstart == 1) player.sendCommand({PR_PLAY, config.lastStation()});
}

void loop() {
#ifdef MEM_WATCHDOG_AUTOREBOOT
  memWatchdog.onStableRun();
  if (memWatchdog.rebootArmed() && !memWatchdog.isSuppressed()) {
    // E36AUD0A1: render-only via player error state; ##SYS# diagnostic — not track metadata / не setTitle
    player.setError("LOW RAM: rebooting to recover", false);
    telnet.printf("##SYS#: LOW RAM: rebooting to recover\n");
    delay(2000);
    ESP.restart();
  }
#endif
  telnet.loop();
  if (network.status == CONNECTED || network.status==SDREADY) {
    player.loop();
    //loopControls();
  }
  loopControls();
  netserver.loop();
}

#include "core/audiohandlers.h"

/**************************************************************************
*   Plugin BacklightDown.
*   Ver.1.0 (Maleksm) for  20.12.2024
***************************************************************************/
#if (defined(AUTOBACKLIGHT)) && (defined(DOWN_LEVEL) && defined(DOWN_INTERVAL))
#include <Ticker.h>

/*    */
#ifdef DOWN_LEVEL
  const uint8_t brightness_down_level = DOWN_LEVEL;
#else
  const uint8_t brightness_down_level = 2;   /* lowest level brightness (from 0 to 255) */
#endif
#ifdef DOWN_INTERVAL
  const uint16_t Out_Interval = DOWN_INTERVAL;
#else
  const uint16_t Out_Interval = 60;         /* interval for BacklightDown in sec (60 sec = 1 min) */
#endif

  Ticker backlightTicker;
  uint8_t current_brightness;

  void backlightDown()       /* function Backlight Down */
  {
  if(network.status!=SOFT_AP)   {
    backlightTicker.detach();
    #if DSP_MODEL==DSP_UEDX48480021
      // UEDX48480021: Active LOW backlight - invert logic
      current_brightness = map(config.store.brightness, 0, 100, 255, 0);
      while(current_brightness < (255 - brightness_down_level)) {
          current_brightness += 2;
          if(current_brightness > (255 - brightness_down_level)) current_brightness = (255 - brightness_down_level);
          analogWrite(GFX_BL, current_brightness);
          vTaskDelay(30);					                    }
    #else
      // Standard backlight logic
      current_brightness = map(config.store.brightness, 0, 100, 0, 255);
      while(current_brightness > brightness_down_level) {
          current_brightness -= 2;
          if(current_brightness < brightness_down_level) current_brightness = brightness_down_level;
          analogWrite(GFX_BL, current_brightness);
          vTaskDelay(30);					                    }
    #endif
						                    }
  }

  void brightnessOn()          /* function Backlight ON */
  { backlightTicker.detach();
    #if DSP_MODEL==DSP_UEDX48480021
      // UEDX48480021: Active LOW backlight - invert mapping (same as display driver)
      analogWrite(GFX_BL, map(config.store.brightness, 0, 100, 255, 0));
    #else
      // Standard backlight logic
      analogWrite(GFX_BL, map(config.store.brightness, 0, 100, 0, 255));
    #endif
    backlightTicker.attach(Out_Interval, backlightDown);
  }

  void yoradio_on_setup() { brightnessOn(); } 			/* Backlight ON for Setup */
  void player_on_track_change() { brightnessOn(); } 		/* Backlight ON for track change */
  void player_on_start_play() { brightnessOn(); } 		/* Backlight ON for start play */
  void player_on_stop_play() { brightnessOn(); } 		/* Backlight ON for stop play */
  void ctrls_on_loop() { 							/* Backlight ON for reg. operations */
    if(!config.isScreensaver) {
      static uint32_t prevBlPinMillis;
      if((display.mode()!=PLAYER) && (millis()-prevBlPinMillis>1000))
        { prevBlPinMillis=millis();
        brightnessOn(); }
                                    }
    }
#endif  /*  #if defined(AUTOBACKLIGHT) */

// I2S hook: reserved for future LVGL spectrum feed when config.store.usespectrum is wired (8.1F-B).
// Хук I2S: зарезервирован под будущий LVGL-спектр по config.store.usespectrum (8.1F-B).
void audio_process_i2s(int16_t* outBuff, int32_t validSamples, bool *continueI2S) {
    (void)outBuff;
    (void)validSamples;
    *continueI2S = true;
}
