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
  memWatchdog.tick();
  if (memWatchdog.rebootArmed() && !memWatchdog.isSuppressed()) {
    memWatchdog.printRebootDiagnostic();
    if (memWatchdog.isFunctionalReboot()) {
      // E36MEM0C: functional fail-storm — not track metadata / не setTitle
      player.setError("Playback recovery: rebooting", false);
      telnet.printf("##SYS#: Playback recovery: rebooting\n");
    } else {
      // E36AUD0A1 / E36MEM0B: memory-critical recovery
      player.setError("LOW RAM: rebooting to recover", false);
      telnet.printf("##SYS#: LOW RAM: rebooting to recover\n");
    }
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

// I2S hook: reserved for future LVGL spectrum feed when config.store.usespectrum is wired (8.1F-B).
// Хук I2S: зарезервирован под будущий LVGL-спектр по config.store.usespectrum (8.1F-B).
void audio_process_i2s(int16_t* outBuff, int32_t validSamples, bool *continueI2S) {
    (void)outBuff;
    (void)validSamples;
    *continueI2S = true;
}
