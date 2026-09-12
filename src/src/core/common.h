#ifndef COMMON_H
#define COMMON_H

enum displayMode_e { PLAYER, VOL, STATIONS, LOST, UPDATING, INFO, SETTINGS, WIFI, SCREENSAVER, SCREENBLANK };

// REFRESH_MAIN (8.1H-I-B): explicit "redraw Main from settings/reset" action, not a mode.
// REFRESH_MAIN (8.1H-I-B): явное действие "перерисовать Main после настроек/сброса", не режим.
// SLEEP_DEVICE_NOW: run the managed Sleep Device shutdown pipeline immediately, on DspTask -
// e.g. from a telnet/serial test command. Not a mode change; see sleep_timer_request_device_sleep().
// SLEEP_DEVICE_NOW: немедленно запустить управляемый Sleep Device shutdown на DspTask -
// например, из telnet/serial тестовой команды. Не смена режима.
enum displayRequestType_e { BOOTSTRING, NEWMODE, CLOCK, NEWTITLE, NEWSTATION, DRAWVOL, DBITRATE, DSP_START, WAITFORSD, MAIN_BG_FS_UPDATED, ART_FS_UPDATED, SET_THEME_PRESET, CUSTOM_THEME_FILE_UPDATED, REFRESH_MAIN, RGB_RESYNC, SLEEP_DEVICE_NOW, NOPE };
struct requestParams_t
{
  displayRequestType_e type;
  int payload;
};

enum controlEvt_e { EVT_NONE=255, EVT_BTNLEFT=0, EVT_BTNCENTER=1, EVT_BTNRIGHT=2, EVT_ENCBTNB=3, EVT_BTNUP=4, EVT_BTNDOWN=5, EVT_ENC2BTNB=6, EVT_BTNMODE=7, EVT_BTNMUTE=8 };



#endif
