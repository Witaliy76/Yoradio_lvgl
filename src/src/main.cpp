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
#include "core/sleep_timer.h"
#include "platform/storage/storage_owner.h"
#include "platform/storage/storage_sdspi.h"
#include "platform/storage/storage_engineering_log.h"
#include "displays/display_port.h"

// AI subsystem (Stage 6.0) / AI-подсистема
#include "ai/ai_subsystem.h"

#if defined(YORADIO_ARDUINO_LOOP_STACK_BYTES)
// Keep the synchronous HTTPS call stack in the ordinary internal task memory.
// Стек синхронного HTTPS остаётся в штатной внутренней памяти задачи.
SET_LOOP_TASK_STACK_SIZE(YORADIO_ARDUINO_LOOP_STACK_BYTES)
#endif

#if DSP_HSPI || TS_HSPI || VS_HSPI
SPIClass  SPI2(HOOPSENb);
#endif

extern __attribute__((weak)) void yoradio_on_setup();

void setup() {
  Serial.begin(115200);
  // Deep-sleep wake pin is still held in RTC IO here. On 4848S040 that pin is GPIO0, which
  // doubles as the BOOT button and as the RGB panel's ST7701_R4 line, so it must be handed
  // back to digital GPIO before config.init()/display.init() bring the panel up.
  // Wake-пин после deep sleep всё ещё в RTC IO. На 4848S040 это GPIO0: он же кнопка BOOT
  // и линия ST7701_R4 RGB-панели, поэтому его нужно вернуть в обычный GPIO
  // до config.init()/display.init(), которые поднимают панель.
  sleep_wakeup_early_init();
  if(REAL_LEDBUILTIN!=255) pinMode(REAL_LEDBUILTIN, OUTPUT);
  // Owner enters SPI mode and fully releases the bus before Type9.
  // Owner входит в SPI mode и полностью освобождает шину до Type9.
#if YORADIO_STORAGE_OWNER_ENABLED
  const auto& sd_config=yoradio::storage::sdspi::bootConfig();
  Serial.printf("[S07-ENG] fw=%s owner=%u transport=%u CS=%d MOSI=%d MISO=%d SCK=%d LCD_CS=%d legacy_CS=%d USE_SD=%d\n",
      YOVERSION,static_cast<unsigned>(sd_config.enabled),
      static_cast<unsigned>(yoradio::kResolvedStorageConfig.transport),
      sd_config.cs,sd_config.mosi,sd_config.miso,sd_config.sck,sd_config.lcd_cs,
      SDC_CS,static_cast<int>(USE_SD));
  Serial.printf("[S07-ENG] mount=%s LFN_HEAP=%u MAX_LFN=%u UTF8=%u CP=%u autoformat=0 ssr_timeout_fallback=%u\n",
      sd_config.path,static_cast<unsigned>(CONFIG_FATFS_LFN_HEAP),
      static_cast<unsigned>(CONFIG_FATFS_MAX_LFN),static_cast<unsigned>(CONFIG_FATFS_API_ENCODING_UTF_8),
      static_cast<unsigned>(CONFIG_FATFS_CODEPAGE),
      static_cast<unsigned>(yoradio::storage::sdspi::compiledSsrTimeoutFallback()));
#endif
  yoradio::storage::StorageOwner::instance().prepareBeforeDisplay();
  if (yoradio_on_setup) yoradio_on_setup();
  
  // AI subsystem: explicit init before config (same order as former AIPlugin path)
  // AI-подсистема: явный init до config (тот же порядок, что у AIPlugin)
  aiSubsystem.init();
  aiSubsystem.onSetup();
  config.init();
  // Publish the restored snapshot before display.init() starts DspTask/LVGL.
  flipTS(config.store.fliptouch);
#ifdef MEM_WATCHDOG_AUTOREBOOT
  memWatchdog.onBoot();
#endif
  display.init();
  S07_STORAGE_LOG("[S07-ENG] display init returned; Type9/panel_ready=%u\n",
      static_cast<unsigned>(DisplayPort::rgbScanoutStatus().panelReady));
  auto& storage = yoradio::storage::StorageOwner::instance();
  storage.mountAfterDisplay(DisplayPort::rgbScanoutStatus().panelReady);
  const auto storage_status = storage.status();
  Serial.printf("[Storage] state=%u ready=%u generation=%llu error=%u native=%ld cleanup=%ld\n",
      static_cast<unsigned>(storage_status.state), static_cast<unsigned>(storage_status.ready),
      static_cast<unsigned long long>(storage_status.generation),
      static_cast<unsigned>(storage_status.error), static_cast<long>(storage_status.native_error),
      static_cast<long>(storage_status.cleanup_error));
  yoradio::storage::g48("A-mount");

  // Block 8-E18D: SpectrumAnalyzer runtime disabled (see audio_process_i2s).
  // Block 8-E18D: SpectrumAnalyzer отключён в runtime (см. audio_process_i2s).

  player.init();
  yoradio::storage::g48("B-player-init");
  config.initPlaylistMode(); // Radio catalog must also be available without Wi-Fi
  network.begin();
  yoradio::storage::g48("C-network");
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
    player.lockOutput=false;
    return;
  }
  if(SDC_CS!=255) {
    display.putRequest(WAITFORSD, 0);
    Serial.print("##[BOOT]#\tSD search\t");
  }
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
  // Stock boot start: last radio station only. SD resume after reboot is not this path.
  // Штатный старт: только последняя радиостанция. Возобновление SD-файла здесь не делается.
  if (config.store.smartstart == 1) player.sendCommand({PR_PLAY, config.lastStation()});
}

void loop() {
  // Consume fault signals on the lifecycle owner task. No automatic teardown,
  // remount, retry or reboot; clients must close their own handles first.
  yoradio::storage::StorageOwner::instance().processFault();
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
  player.loop(); // control/STOP must run even without Wi-Fi
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
