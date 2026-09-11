#include "Arduino.h"
#include "controls.h"
#include "options.h"
#include "config.h"
#include "player.h"
#include "display.h"
#include "network.h"
#include "netserver.h"

long encOldPosition  = 0;
long enc2OldPosition  = 0;
int lpId = -1;

#define ISPUSHBUTTONS BTN_LEFT!=255 || BTN_CENTER!=255 || BTN_RIGHT!=255 || ENC_BTNB!=255 || BTN_UP!=255 || BTN_DOWN!=255 || ENC2_BTNB!=255 || BTN_MODE!=255 || BTN_MUTE!=255
#if ISPUSHBUTTONS
#include "../OneButton/OneButton.h"
OneButton button[] {{BTN_LEFT, true, BTN_INTERNALPULLUP}, {BTN_CENTER, true, BTN_INTERNALPULLUP}, {BTN_RIGHT, true, BTN_INTERNALPULLUP}, {ENC_BTNB, true, ENC_INTERNALPULLUP}, {BTN_UP, true, BTN_INTERNALPULLUP}, {BTN_DOWN, true, BTN_INTERNALPULLUP}, {ENC2_BTNB, true, ENC2_INTERNALPULLUP}, {BTN_MODE, true, BTN_INTERNALPULLUP}, {BTN_MUTE, true, BTN_INTERNALPULLUP}};
constexpr uint8_t nrOfButtons = sizeof(button) / sizeof(button[0]);
#endif

#if ENC_HALFQUARD==false
#define ENCODER_STEPS 4
#elif ENC_HALFQUARD==true
#define ENCODER_STEPS 2
#elif ENC_HALFQUARD==255
#define ENCODER_STEPS 1
#endif
#if ENC2_HALFQUARD==false
#define ENCODER2_STEPS 4
#elif ENC2_HALFQUARD==true
#define ENCODER2_STEPS 2
#elif ENC2_HALFQUARD==255
#define ENCODER2_STEPS 1
#endif

#if (ENC_BTNL!=255 && ENC_BTNR!=255) || (ENC2_BTNL!=255 && ENC2_BTNR!=255)
  #if (ENC_BTNL!=255 && ENC_BTNR!=255)
    yoEncoder encoder = yoEncoder(ENC_BTNL, ENC_BTNR, ENCODER_STEPS, ENC_INTERNALPULLUP);
  #endif
  #if (ENC2_BTNL!=255 && ENC2_BTNR!=255)
    yoEncoder encoder2 = yoEncoder(ENC2_BTNL, ENC2_BTNR, ENCODER2_STEPS, ENC2_INTERNALPULLUP);
  #endif
#endif

#if (TS_MODEL!=TS_MODEL_UNDEFINED)
  #include "touchscreen.h"
  TouchScreen touchscreen;
#endif

#if IR_PIN!=255
#include <assert.h>

#include "../IRremoteESP8266/IRrecv.h"
#include "../IRremoteESP8266/IRremoteESP8266.h"
#include "../IRremoteESP8266/IRac.h"
#include "../IRremoteESP8266/IRtext.h"
#include "../IRremoteESP8266/IRutils.h"
uint8_t irVolRepeat = 0;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = IR_TIMEOUT;
const uint16_t kMinUnknownSize = 12;
#define LEGACY_TIMING_INFO false

IRrecv irrecv(IR_PIN, kCaptureBufferSize, kTimeout, true);
decode_results irResults;
#endif

#if ENC_BTNL!=255
void IRAM_ATTR readEncoderISR()
{
  if((SDC_CS==255 && display.mode()==LOST) || display.mode()==UPDATING) return;
  encoder.readEncoder_ISR();
}
#endif
#if ENC2_BTNL!=255
void IRAM_ATTR readEncoder2ISR()
{
  if((SDC_CS==255 && display.mode()==LOST) || display.mode()==UPDATING) return;
  encoder2.readEncoder_ISR();
}
#endif

void initControls() {
  
#if ENC_BTNL!=255
  encoder.begin();
  encoder.setup(readEncoderISR);
  encoder.setBoundaries(0, 254, true);
  encoder.setAcceleration(config.store.encacc);
#endif
#if ENC2_BTNL!=255
  encoder2.begin();
  encoder2.setup(readEncoder2ISR);
  encoder2.setBoundaries(0, 254, true);
  encoder2.setAcceleration(config.store.encacc);
#endif

#if ISPUSHBUTTONS
  for (int i = 0; i < nrOfButtons; i++)
  {
    if ((i == 0 && BTN_LEFT == 255) || (i == 1 && BTN_CENTER == 255) || (i == 2 && BTN_RIGHT == 255) || (i == 3 && ENC_BTNB == 255) || (i == 4 && BTN_UP == 255) || (i == 5 && BTN_DOWN == 255) || (i == 6 && ENC2_BTNB == 255) || (i == 7 && BTN_MODE == 255) || (i == 8 && BTN_MUTE == 255)) continue;
    button[i].attachClick([](void* p) {
      onBtnClick((int)p);
    }, (void*)i);
    button[i].attachDoubleClick([](void* p) {
      onBtnDoubleClick((int)p);
    }, (void*)i);
    button[i].attachLongPressStart([](void* p) {
      onBtnLongPressStart((int)p);
    }, (void*)i);
    button[i].attachLongPressStop([](void* p) {
      onBtnLongPressStop((int)p);
    }, (void*)i);
    button[i].setClickTicks(BTN_CLICK_TICKS);
    button[i].setPressTicks(BTN_PRESS_TICKS);
  }
#endif
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
  // Defer touch init to first loop() — avoids LoadProhibited/IWDT when GT911 I2C init runs in setup (e.g. 4848S040)
  // Инициализация тача в loop(), чтобы не падать в setup
#endif
#if IR_PIN!=255
  pinMode(IR_PIN, INPUT);
  assert(irutils::lowLevelSanityCheck() == 0);
#if DECODE_HASH
  irrecv.setUnknownThreshold(kMinUnknownSize);
#endif  // DECODE_HASH
  irrecv.setTolerance(config.store.irtlp);
  irrecv.enableIRIn();
#endif // IR_PIN!=255
}

void loopControls() {
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
  static bool s_touchInitDone = false;
  if (!s_touchInitDone) {
    s_touchInitDone = true;
    touchscreen.init();
  }
#endif
  if(display.mode()==UPDATING) return;
  if(SDC_CS==255 && display.mode()==LOST) return;
  if(ctrls_on_loop) ctrls_on_loop();
#if ENC_BTNL!=255
  encoder1Loop();
#endif
#if ENC2_BTNL!=255
  encoder2Loop();
#endif
#if ISPUSHBUTTONS
  for (unsigned i = 0; i < nrOfButtons; i++)
  {
    if ((i == 0 && BTN_LEFT == 255) || (i == 1 && BTN_CENTER == 255) || (i == 2 && BTN_RIGHT == 255) || (i == 3 && ENC_BTNB == 255) || (i == 4 && BTN_UP == 255) || (i == 5 && BTN_DOWN == 255) || (i == 6 && ENC2_BTNB == 255) || (i == 8 && BTN_MUTE == 255)) continue;
    button[i].tick();
    if (lpId >= 0) {
      onBtnDuringLongPress(lpId);
    }
  }
#endif
#if IR_PIN!=255
  irLoop();
#endif
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
  if (network.status == CONNECTED || network.status == SDREADY) {
#if defined(DUMMYDISPLAY)
    // Headless / no panel: raw touch loop still used when hardware present.
    // Без дисплея: сырой touch loop, если тач подключён.
    touchscreen.loop();
#endif
    // 8-E19C-1: LVGL-only product — touch via LVGL indev; do not call touchscreen.loop().
    // 8-E19C-1: LVGL-only — touch через LVGL indev; touchscreen.loop() не вызываем.
  }
#endif
}
#if ENC_BTNL!=255 || ENC2_BTNL!=255
void encodersLoop(yoEncoder *enc, bool first){
  if (network.status != CONNECTED && network.status!=SDREADY) return;
  if(display.mode()==LOST) return;
  int8_t encoderDelta = enc->encoderChanged();
  if (encoderDelta!=0)
  {
    uint8_t encBtnState = digitalRead(first?ENC_BTNB:ENC2_BTNB);
#   if defined(DUMMYDISPLAY)
    first = first?(first && encBtnState):(!encBtnState);
    if(first){
      int nv = config.store.volume+encoderDelta;
      if(nv<0) nv=0;
      if(nv>254) nv=254;
      player.setVol((uint8_t)nv);  
    }else{
      if(encoderDelta > 0) player.next(); else player.prev();
    }
#   else
    if(first){
      controlsEvent(encoderDelta > 0, encoderDelta);
    }else{
      if (encBtnState == HIGH && display.mode() == PLAYER) {
        if(config.store.skipPlaylistUpDown){
          if(encoderDelta > 0) player.next(); else player.prev();
          return;
        }
        lvgl_ui::openStationPageFromProductInput();
        while(display.mode() != STATIONS) {delay(10);}
      }
      controlsEvent(encoderDelta > 0, encoderDelta);
    }
#   endif
  }
}
#endif

#if ENC_BTNL!=255
void encoder1Loop() {
  encodersLoop(&encoder, true);
}
#endif

#if ENC2_BTNL!=255
void encoder2Loop() {
  encodersLoop(&encoder2, false);
}
#endif

#if IR_PIN!=255
void irBlink() {
  if(REAL_LEDBUILTIN==255) return;
  if (player.status() == STOPPED) {
    for (uint8_t i = 0; i < 7; i++) {
      digitalWrite(REAL_LEDBUILTIN, !digitalRead(REAL_LEDBUILTIN));
      delay(100);
    }
  }
}

// Block 8-E9: direct station index via IR digits — no NUMBERS display mode on LVGL product.
// Follow-up: optional LVGL typed-number hint on Main (not Block 8); no auto-play per digit.
static void irClearDirectStationAccumulator() {
  display.numOfNextStation = 0;
}

static bool irConfirmDirectStationPlay() {
  const uint16_t n = display.numOfNextStation;
  if (n == 0) {
    return false;
  }
  if (n > config.store.countStation) {
    irClearDirectStationAccumulator();
    return false;
  }
  player.sendCommand({PR_PLAY, n});
  irClearDirectStationAccumulator();
  return true;
}

void irNumber(uint8_t num) {
  uint16_t s;
  if (display.numOfNextStation == 0 && num == 0) return;
  if (display.numOfNextStation > UINT16_MAX / 10) return;
  s = display.numOfNextStation * 10 + num;
  if (s > config.store.countStation) return;
  display.numOfNextStation = s;
  // Block 8-E9: confirm on IR PLAY only; no NEWMODE NUMBERS / NEXTSTATION (legacy UI).
  // Not restored: legacy 10s returnPlayer ticker (was tied to _drawNextStationNum).
}

void irLoop() {
  if (irrecv.decode(&irResults)) {
    if(irResults.value<256) return;
    if (netserver.irRecordEnable) {
      Serial.print(resultToHumanReadableBasic(&irResults));
      Serial.println("--------------------------");
      config.ircodes.irVals[config.irindex][config.irchck]=irResults.value;
      netserver.irToWs(typeToString(irResults.decode_type, irResults.repeat).c_str(), irResults.value);
      return;
    }
    if (!irResults.repeat/* && irResults.command!=0*/) {
      irVolRepeat = 0;
    }
    switch (irVolRepeat) {
      case 1: {
          controlsEvent(display.mode() == STATIONS ? false : true);
          break;
        }
      case 2: {
          controlsEvent(display.mode() == STATIONS ? true : false);
          break;
        }
    }
    for(int target=0; target<17; target++){
      for(int j=0; j<3; j++){
        if(config.ircodes.irVals[target][j]==irResults.value){
          if (network.status != CONNECTED && network.status!=SDREADY && target!=IR_AST) return;
          if(target!=IR_AST && display.mode()==LOST) return;
          if (display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) {
            display.putRequest(NEWMODE, PLAYER);
            config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
            config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
            return;
          }
          switch (target){
            case IR_PLAY: {
                irBlink();
                if (irConfirmDirectStationPlay()) {
                  break;
                }
                onBtnClick(1);
                break;
              }
            case IR_PREV: {
                player.prev();
                break;
              }
            case IR_NEXT: {
                player.next();
                break;
              }
            case IR_UP: {
                controlsEvent(display.mode() == STATIONS ? false : true);
                irVolRepeat = 1;
                break;
              }
            case IR_DOWN: {
                controlsEvent(display.mode() == STATIONS ? true : false);
                irVolRepeat = 2;
                break;
              }
            case IR_HASH: {
                if (display.numOfNextStation > 0) {
                  irClearDirectStationAccumulator();
                  break;
                }
                lvgl_ui::toggleStationListUiFromProductInput();
                break;
              }
            case IR_0: {
                irNumber(0);
                break;
              }
            case IR_1: {
                irNumber(1);
                break;
              }
            case IR_2: {
                irNumber(2);
                break;
              }
            case IR_3: {
                irNumber(3);
                break;
              }
            case IR_4: {
                irNumber(4);
                break;
              }
            case IR_5: {
                irNumber(5);
                break;
              }
            case IR_6: {
                irNumber(6);
                break;
              }
            case IR_7: {
                irNumber(7);
                break;
              }
            case IR_8: {
                irNumber(8);
                break;
              }
            case IR_9: {
                irNumber(9);
                break;
              }
            case IR_AST: {
                //ESP.restart();
                onBtnClick(EVT_BTNMODE);
                break;
              }
          } /* switch (target) */
          target=17;
          break;
        } /* if(config.ircodes.irVals[target][j]==irResults.value) */
      }   /* for(int j=0; j<3; j++) */
    }     /* for(int target=0; target<16; target++) */
  }       /* if (irrecv.decode(&irResults)) */
}
#endif // if IR_PIN!=255

void onBtnLongPressStart(int id) {
  switch ((controlEvt_e)id) {
    case EVT_BTNLEFT:
    case EVT_BTNRIGHT:
    case EVT_BTNUP:
    case EVT_BTNDOWN: {
        lpId = id;
        break;
      }
    case EVT_BTNCENTER:
    case EVT_ENCBTNB: {
#       if defined(DUMMYDISPLAY)
        break;
#       endif
        lvgl_ui::toggleStationListUiFromProductInput();
        break;
      }
    case EVT_ENC2BTNB: {
#       if defined(DUMMYDISPLAY)
        break;
#       endif
        display.putRequest(NEWMODE, display.mode() == PLAYER ? VOL : PLAYER);
        break;
      }
    case EVT_BTNMODE: {
        // Block 8.1H-G-B: legacy SLEEPING display mode removed; button inert until explicit LVGL sleep UI.
        // Block 8.1H-G-B: legacy-режим SLEEPING удалён; кнопка без действия до явного LVGL sleep UI.
        break;
      }
    default: break;
  }
}

void onBtnLongPressStop(int id) {
  switch ((controlEvt_e)id) {
    case EVT_BTNLEFT:
    case EVT_BTNRIGHT:
    case EVT_BTNUP:
    case EVT_BTNDOWN: {
        lpId = -1;
        break;
      }
    case EVT_BTNMODE: {
        config.doSleepW();
        break;
      }
    default:
        break;
  }
}

unsigned long lpdelay;
boolean checklpdelay(int m, unsigned long &tstamp) {
  if (millis() - tstamp > m) {
    tstamp = millis();
    return true;
  } else {
    return false;
  }
}

void onBtnDuringLongPress(int id) {
  if (network.status != CONNECTED && network.status!=SDREADY) return;
  if (checklpdelay(BTN_LONGPRESS_LOOP_DELAY, lpdelay)) {
    switch ((controlEvt_e)id) {
      case EVT_BTNLEFT: {
          controlsEvent(false);
          break;
        }
      case EVT_BTNRIGHT: {
          controlsEvent(true);
          break;
        }
      case EVT_BTNUP:
      case EVT_BTNDOWN: {
          if (display.mode() == PLAYER) {
            lvgl_ui::openStationPageFromProductInput();
          }
          break;
        }
      default:
          break;
    }
  }
}

void controlsEvent(bool toRight, int8_t volDelta) {
  if (display.numOfNextStation > 0) {
    display.numOfNextStation = 0;
  }
  if (display.mode() != STATIONS && !display.isStationsChanging()) {
    #if !defined(DUMMYDISPLAY)
      if(!display.isVolumeChanging()) {
      display.putRequest(NEWMODE, VOL);
      }
    #endif
    if(volDelta!=0){
      int nv = config.store.volume+volDelta;
      if(nv<0) nv=0;
      if(nv>254) nv=254;
      player.setVol((uint8_t)nv);
    }else{
      player.stepVol(toRight);
    }
  }
  if (display.mode() == STATIONS) {
    // Block 8-E12 / 8-E19C-1: no legacy DRAWPLAYLIST navigation on LVGL product path.
    // Block 8-E12 / 8-E19C-1: без legacy DRAWPLAYLIST на LVGL product path.
    return;
  }
}

void onBtnClick(int id) {
  bool passBnCenter = (controlEvt_e)id==EVT_BTNCENTER || (controlEvt_e)id==EVT_ENCBTNB || (controlEvt_e)id==EVT_ENC2BTNB;
  controlEvt_e btnid = static_cast<controlEvt_e>(id);
  if (network.status != CONNECTED && network.status!=SDREADY && (controlEvt_e)id!=EVT_BTNMODE && !passBnCenter) return;
  switch (btnid) {
    case EVT_BTNLEFT: {
        controlsEvent(false);
        break;
      }
    case EVT_BTNCENTER:
    case EVT_ENCBTNB:
    case EVT_ENC2BTNB: {
        if (display.numOfNextStation > 0) {
          display.numOfNextStation = 0;
        }
        if (display.mode() == PLAYER) {
          player.toggle();
        }
        if (display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) {
          display.putRequest(NEWMODE, PLAYER);
          config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
          config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
          #ifdef DSP_LCD
            delay(200);
          #endif
        }
        if (display.mode() == STATIONS) {
          display.putRequest(NEWMODE, PLAYER);
          player.sendCommand({PR_PLAY, display.currentPlItem});
        }
        if(network.status==SOFT_AP || display.mode()==LOST){
          #ifdef USE_SD
            config.changeMode();
          #endif
        }
        break;
      }
    case EVT_BTNRIGHT: {
        controlsEvent(true);
        break;
      }
    case EVT_BTNUP:
    case EVT_BTNDOWN: {
        if (display.mode() == PLAYER) {
          if(config.store.skipPlaylistUpDown || ENC2_BTNL!=255){
            if (id == EVT_BTNUP) {
              player.prev();
            } else {
              player.next();
            }
          }else{
            lvgl_ui::openStationPageFromProductInput();
          }
        }
        break;
      }
    #ifdef USE_SD
    case EVT_BTNMODE: {
      config.changeMode();
      break;
    }
    #endif
    case EVT_BTNMUTE: {
        // Screensaver keeps its wake-first contract: the first press only wakes, as for the
        // centre button above; the mute toggle needs a second, deliberate press.
        // Скринсейвер сохраняет контракт "первое нажатие только будит": как у центральной
        // кнопки выше; само переключение MUTE - по второму, осознанному нажатию.
        if (display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) {
          display.putRequest(NEWMODE, PLAYER);
          config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
          config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
          break;
        }
        // One semantic action, delivered through the player queue - no direct codec/LVGL touch here.
        // Одно семантическое действие через очередь плеера - без прямого доступа к кодеку/LVGL.
        player.requestMute(PMUTE_TOGGLE);
        break;
      }
    default: break;
  }
}

void onBtnDoubleClick(int id) {
  if (display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) {
    display.putRequest(NEWMODE, PLAYER);
    config.screensaverTicks=SCREENSAVERSTARTUPDELAY;
    config.screensaverPlayingTicks=SCREENSAVERSTARTUPDELAY;
    return;
  }
  switch ((controlEvt_e)id) {
    case EVT_BTNLEFT: {
        if (display.mode() != PLAYER) return;
        if (network.status != CONNECTED && network.status!=SDREADY) return;
        player.prev();
        break;
      }
    case EVT_BTNCENTER:
    case EVT_ENCBTNB:
    case EVT_ENC2BTNB: {
        //display.putRequest(NEWMODE, display.mode() == PLAYER ? VOL : PLAYER);
        onBtnClick(EVT_BTNMODE);
        break;
      }
    case EVT_BTNRIGHT: {
        if (display.mode() != PLAYER) return;
        if (network.status != CONNECTED && network.status!=SDREADY) return;
        player.next();
        break;
      }
    default:
        break;
  }
}
void setIRTolerance(uint8_t tl){
  config.saveValue(&config.store.irtlp, tl);
#if IR_PIN!=255
  irrecv.setTolerance(config.store.irtlp);
#endif
}
void setEncAcceleration(uint16_t acc){
  config.saveValue(&config.store.encacc, acc);
#if ENC_BTNL!=255
  encoder.setAcceleration(config.store.encacc);
#endif
#if ENC2_BTNL!=255
  encoder2.setAcceleration(config.store.encacc);
#endif
}
void flipTS(){
#if (TS_MODEL!=TS_MODEL_UNDEFINED)
  touchscreen.flip();
#endif
}
