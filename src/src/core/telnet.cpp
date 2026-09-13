#include <stdarg.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include "WiFi.h"

#include "config.h"
#include "player.h"
#include "network.h"
#include "display.h"
#include "telnet.h"
#include "sleep_timer.h"
#include "../ai/ai_log.h"  // Для aiLogSetBootDone / For aiLogSetBootDone

Telnet telnet;

namespace {

struct ParsedMinutesCommand {
  bool matched = false;
  bool valid = false;
  bool has_value = false;
  uint16_t value = 0;
};

// Exact runtime command parser: command boundary, digits only, no negative/overflow/junk tail.
// Точный parser runtime-команд: граница имени, только цифры, без minus/overflow/мусорного хвоста.
ParsedMinutesCommand parse_minutes_command(const char* input, const char* command,
                                           bool allow_bare) {
  ParsedMinutesCommand parsed;
  if (!input || !command) return parsed;
  const size_t command_len = strlen(command);
  if (strncmp(input, command, command_len) != 0) return parsed;
  const char boundary = input[command_len];
  if (boundary != '\0' && boundary != ' ') return parsed;
  parsed.matched = true;
  if (boundary == '\0') {
    parsed.valid = allow_bare;
    return parsed;
  }

  const char* value_text = input + command_len;
  while (*value_text == ' ') ++value_text;
  if (*value_text < '0' || *value_text > '9') return parsed;
  errno = 0;
  char* end = nullptr;
  const unsigned long value = strtoul(value_text, &end, 10);
  if (errno != 0 || !end || *end != '\0' || value > kTimerMaxMinutes) return parsed;
  parsed.valid = true;
  parsed.has_value = true;
  parsed.value = static_cast<uint16_t>(value);
  return parsed;
}

ParsedMinutesCommand parse_timer_aliases(const char* input, const char* command,
                                         const char* cli_command, bool allow_bare) {
  ParsedMinutesCommand parsed = parse_minutes_command(input, command, allow_bare);
  if (!parsed.matched) parsed = parse_minutes_command(input, cli_command, allow_bare);
  return parsed;
}

struct ParsedLegacySleep {
  bool matched = false;
  bool valid = false;
  bool delayed = false;
  uint16_t wake_minutes = 0;
  uint16_t after_minutes = 0;
};

ParsedLegacySleep parse_legacy_sleep(const char* input) {
  ParsedLegacySleep parsed;
  if (!input) return parsed;
  parsed.matched = strncmp(input, "sleep ", 6) == 0 || strncmp(input, "sleep(", 6) == 0 ||
                   strncmp(input, "cli.sleep(", 10) == 0;
  if (!parsed.matched) return parsed;

  int wake = 0;
  int after = 0;
  int consumed = -1;
  const bool two =
      (sscanf(input, "sleep(%d,%d)%n", &wake, &after, &consumed) == 2 &&
       consumed >= 0 && input[consumed] == '\0') ||
      (sscanf(input, "cli.sleep(\"%d\",\"%d\")%n", &wake, &after, &consumed) == 2 &&
       consumed >= 0 && input[consumed] == '\0') ||
      (sscanf(input, "sleep %d %d%n", &wake, &after, &consumed) == 2 &&
       consumed >= 0 && input[consumed] == '\0');
  if (two) {
    parsed.delayed = true;
    parsed.valid = wake > 0 && wake <= kTimerMaxMinutes &&
                   after > 0 && after <= kTimerMaxMinutes;
  } else {
    consumed = -1;
    const bool one =
        (sscanf(input, "sleep(%d)%n", &wake, &consumed) == 1 &&
         consumed >= 0 && input[consumed] == '\0') ||
        (sscanf(input, "cli.sleep(\"%d\")%n", &wake, &consumed) == 1 &&
         consumed >= 0 && input[consumed] == '\0') ||
        (sscanf(input, "sleep %d%n", &wake, &consumed) == 1 &&
         consumed >= 0 && input[consumed] == '\0');
    parsed.valid = one && wake > 0 && wake <= kTimerMaxMinutes;
  }
  if (parsed.valid) {
    parsed.wake_minutes = static_cast<uint16_t>(wake);
    parsed.after_minutes = static_cast<uint16_t>(after);
  }
  return parsed;
}

}  // namespace

void Telnet::printDiagBody(uint8_t clientId, const char* body) {
  if (!body || body[0] == '\0') return;
  if (clientId > MAX_TLN_CLIENTS) {
    Serial.print(body);
    return;
  }
  if (clients[clientId] && clients[clientId].connected()) {
    clients[clientId].print(body);
  }
}

bool Telnet::_isIPSet(IPAddress ip) {
  return ip.toString() == "0.0.0.0";
}

bool Telnet::begin(bool quiet) {
  if(network.status==SDREADY) {
    BOOTLOG("Ready in SD Mode!");
    BOOTLOG("------------------------------------------------");
    Serial.println("##[BOOT]#");
    return true;
  }
  if(!quiet) Serial.print("##[BOOT]#\ttelnet.begin\t");
  if (WiFi.status() == WL_CONNECTED || _isIPSet(WiFi.softAPIP())) {
    server.begin();
    server.setNoDelay(true);
    if(!quiet){
      Serial.println("done");
      Serial.println("##[BOOT]#");
      BOOTLOG("Ready! Go to http:/%s/ to configure", WiFi.localIP().toString().c_str());
      BOOTLOG("------------------------------------------------");
      Serial.println("##[BOOT]#");
      
      // Boot banner завершён - разрешаем AI debug-логи / Boot banner complete - enable AI debug logs
      aiLogSetBootDone(true);
    }
    return true;
  } else {
    return false;
  }
}

void Telnet::stop() {
  server.stop();
}

void Telnet::emptyClientStream(WiFiClient client) {
  client.flush();
  delay(50);
  while (client.available()) {
    client.read();
  }
}

void Telnet::cleanupClients() {
  for (int i = 0; i < MAX_TLN_CLIENTS; i++) {
    if (!clients[i].connected()) {
      if (clients[i]) {
        Serial.printf("Client [%d] is %s\n", i, clients[i].connected() ? "connected" : "disconnected");
        clients[i].stop();
      }
    }
  }
}

void Telnet::handleSerial(){
  if(Serial.available()){
    String request = Serial.readStringUntil('\n'); request.trim();
    on_input(request.c_str(), 100);
  }
}

void Telnet::loop() {
  if(network.status==SDREADY || network.status!=CONNECTED) {
    handleSerial();
    return;
  }
  uint8_t i;
  if (WiFi.status() == WL_CONNECTED) {
    if (server.hasClient()) {
      for (i = 0; i < MAX_TLN_CLIENTS; i++) {
        if (!clients[i] || !clients[i].connected()) {
          if (clients[i]) {
            clients[i].stop();
          }
          clients[i] = server.available();
          if (!clients[i]) Serial.println("available broken");
          on_connect(clients[i].remoteIP().toString().c_str(), i);
          clients[i].setNoDelay(true);
          emptyClientStream(clients[i]);
          break;
        }
      }
      if (i >= MAX_TLN_CLIENTS) {
        server.available().stop();
      }
    }
    for (i = 0; i < MAX_TLN_CLIENTS; i++) {
      if (clients[i] && clients[i].connected() && clients[i].available()) {
        String inputstr = clients[i].readStringUntil('\n');
        inputstr.trim();
        on_input(inputstr.c_str(), i);
      }
    }
  } else {
    for (i = 0; i < MAX_TLN_CLIENTS; i++) {
      if (clients[i]) {
        clients[i].stop();
      }
    }
    delay(1000);
  }
  handleSerial();
  yield();
}

void Telnet::print(const char *buf) {
  for (int id = 0; id < MAX_TLN_CLIENTS; id++) {
    if (clients[id] && clients[id].connected()) {
      print(id, buf);
    }
  }
  Serial.print(buf);
}

void Telnet::print(uint8_t id, const char *buf) {
  if (clients[id] && clients[id].connected()) {
    clients[id].print(buf);
  }
}

void Telnet::printf(const char *format, ...) {
  char buf[MAX_PRINTF_LEN];
  va_list args;
  va_start (args, format );
  vsnprintf(buf, MAX_PRINTF_LEN, format, args);
  va_end (args);
  for (int id = 0; id < MAX_TLN_CLIENTS; id++) {
    if (clients[id] && clients[id].connected()) {
      clients[id].print(buf);
    }
  }
  if (strcmp(buf, "> ") == 0) return;
  //if(strstr(buf,"\n> ")==NULL) Serial.print(buf);
  char *nl = strstr(buf, "\n> ");
  if (nl != NULL) { buf[nl-buf+1] = '\0'; }
  Serial.print(buf);
}

void Telnet::printf(uint8_t id, const char *format, ...) {
  char buf[MAX_PRINTF_LEN];
  va_list argptr;
  va_start(argptr, format);
  vsnprintf(buf, MAX_PRINTF_LEN, format, argptr);
  va_end(argptr);
  if(id>MAX_TLN_CLIENTS){
    Serial.print(buf);
    return;
  }
  if (clients[id] && clients[id].connected()) {
    clients[id].print(buf);
  }
}

void Telnet::on_connect(const char* str, uint8_t clientId) {
  Serial.printf("Telnet: [%d] %s connected\n", clientId, str);
  print(clientId, "\nWelcome to ёRadio!\n(Use ^] + q  to disconnect.)\n> ");
}

void Telnet::info() {
  telnet.printf("##CLI.INFO#\n");
  char timeStringBuff[50];
  // Same TZ suffix as cli.info / requestTimeSync — was hardcoded +03:00 (legacy MSK default).
  // Тот же суффикс TZ, что в cli.info / requestTimeSync — раньше был захардкожен +03:00.
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &network.timeinfo);
  if (config.store.tzHour < 0) {
    // Explicit '-' + absolute hour — %03d on negative tzHour embeds its own sign (asymmetric with +%02d).
    // Явный '-' и модуль часа — %03d для отрицательного tzHour даёт знак внутри числа.
    telnet.printf("##SYS.DATE#: %s-%02d:%02d\n", timeStringBuff, -(int)config.store.tzHour, config.store.tzMin);
  } else {
    telnet.printf("##SYS.DATE#: %s+%02d:%02d\n", timeStringBuff, config.store.tzHour, config.store.tzMin);
  }
  telnet.printf("##CLI.NAMESET#: %d %s\n", config.lastStation(), config.station.name);
  if (player.status() == PLAYING) {
    telnet.printf("##CLI.META#: %s\n",  config.station.title);
  }
  telnet.printf("##CLI.VOL#: %d\n", player.audibleVolume());
  if (player.status() == PLAYING) {
    telnet.printf("##CLI.PLAYING#\n");
  } else {
    telnet.printf("##CLI.STOPPED#\n");
  }
  telnet.printf("> ");
}

void Telnet::on_input(const char* str, uint8_t clientId) {
  if (strlen(str) == 0) return;

  auto print_timer_failure = [&](TimerCommandResult result) {
    switch (result) {
      case TimerCommandResult::ConflictRadioActive:
        printf(clientId, "timer conflict: cancel RADIO timer first\n> ");
        break;
      case TimerCommandResult::ConflictDeepSleepActive:
        printf(clientId, "timer conflict: cancel DEEP SLEEP timer first\n> ");
        break;
      case TimerCommandResult::ShutdownInProgress:
        printf(clientId, "deep sleep shutdown already in progress\n> ");
        break;
      case TimerCommandResult::RadioStartNotAfterStop:
        printf(clientId, "radio start must be later than radio stop\n> ");
        break;
      default:
        printf(clientId, "##CMD_ERROR#\tinvalid timer command <%s>\n> ", str);
        break;
    }
  };

  // Runtime timer commands stay ahead of network.status so the identical parser works over
  // Serial without Wi-Fi. They change runtime snapshots only, never persisted UI presets.
  // Runtime-команды идут до network.status: один parser для Serial/telnet, пресеты не меняются.
  ParsedMinutesCommand parsed =
      parse_timer_aliases(str, "sleeptimer", "cli.sleeptimer", false);
  if (parsed.matched) {
    if (!parsed.valid || !parsed.has_value) {
      print_timer_failure(TimerCommandResult::InvalidMinutes);
      return;
    }
    if (parsed.value == 0) {
      timer_cancel_radio_stop();
      printf(clientId, "radio stop timer cancelled\n> ");
      return;
    }
    const TimerCommandResult result = timer_schedule_radio_stop(parsed.value);
    if (result == TimerCommandResult::Applied) {
      printf(clientId, "radio stop timer set: %u min\n> ",
             static_cast<unsigned>(parsed.value));
    } else {
      print_timer_failure(result);
    }
    return;
  }

  parsed = parse_timer_aliases(str, "playtimer", "cli.playtimer", false);
  if (parsed.matched) {
    if (!parsed.valid || !parsed.has_value) {
      print_timer_failure(TimerCommandResult::InvalidMinutes);
      return;
    }
    if (parsed.value == 0) {
      timer_cancel_radio_start();
      printf(clientId, "radio start timer cancelled\n> ");
      return;
    }
    const TimerCommandResult result = timer_schedule_radio_start(parsed.value);
    if (result == TimerCommandResult::Applied) {
      printf(clientId, "radio start timer set: %u min\n> ",
             static_cast<unsigned>(parsed.value));
    } else {
      print_timer_failure(result);
    }
    return;
  }

  parsed = parse_timer_aliases(str, "deepsleep", "cli.deepsleep", true);
  if (parsed.matched) {
    if (!parsed.valid) {
      print_timer_failure(TimerCommandResult::InvalidMinutes);
      return;
    }
    if (!parsed.has_value) {
      const TimerCommandResult result =
          timer_request_deep_sleep_now(DeepSleepWakeRequest::persistent());
      if (result == TimerCommandResult::Applied) {
        printf(clientId, "deep sleep requested\n> ");
      } else {
        print_timer_failure(result);
      }
      return;
    }
    if (parsed.value == 0) {
      const TimerCommandResult result = timer_cancel_deep_sleep();
      if (result == TimerCommandResult::Cancelled) {
        printf(clientId, "deep sleep timer cancelled\n> ");
      } else {
        print_timer_failure(result);
      }
      return;
    }
    const TimerCommandResult result = timer_schedule_deep_sleep(
        parsed.value, DeepSleepWakeRequest::persistent());
    if (result == TimerCommandResult::Applied) {
      printf(clientId, "deep sleep timer set: %u min\n> ",
             static_cast<unsigned>(parsed.value));
    } else {
      print_timer_failure(result);
    }
    return;
  }

  if(network.status == CONNECTED){
    if (strcmp(str, "cli.prev") == 0 || strcmp(str, "prev") == 0) {
      player.prev();
      return;
    }
    if (strcmp(str, "cli.next") == 0 || strcmp(str, "next") == 0) {
      player.next();
      return;
    }
    if (strcmp(str, "cli.toggle") == 0 || strcmp(str, "toggle") == 0) {
      player.toggle();
      return;
    }
    // Semantic MUTE over telnet/serial - same central action a physical BTN_MUTE or future IR calls.
    // Reply is intentionally silent (like prev/next/toggle): Player::_applyMuteRequest emits the
    // authoritative ##CLI.MUTE# / ##CLI.VOL# a few ms later once the queued request is processed.
    // Семантический MUTE по telnet/serial - то же центральное действие, что кнопка / будущий IR.
    if (strncmp(str, "mute", 4) == 0 || strncmp(str, "cli.mute", 8) == 0) {
      const char* arg = str + (strncmp(str, "cli.", 4) == 0 ? 8 : 4);
      while (*arg == ' ') arg++;
      if (*arg == '\0' || strcmp(arg, "toggle") == 0) {
        player.requestMute(PMUTE_TOGGLE);
      } else if (strcmp(arg, "on") == 0 || strcmp(arg, "1") == 0) {
        player.requestMute(PMUTE_ON);
      } else if (strcmp(arg, "off") == 0 || strcmp(arg, "0") == 0) {
        player.requestMute(PMUTE_OFF);
      } else {
        printf(clientId, "usage: mute [on|off|toggle]\n> ");
      }
      return;
    }
    if (strcmp(str, "cli.stop") == 0 || strcmp(str, "stop") == 0) {
      player.sendCommand({PR_STOP, 0});
      //info();
      return;
    }
    if (strcmp(str, "cli.start") == 0 || strcmp(str, "start") == 0 || strcmp(str, "cli.play") == 0 || strcmp(str, "play") == 0) {
      player.sendCommand({PR_PLAY, config.lastStation()});
      return;
    }
    if (strcmp(str, "cli.vol") == 0 || strcmp(str, "vol") == 0) {
      printf(clientId, "##CLI.VOL#: %d\n> ", player.audibleVolume());
      return;
    }
    if (strcmp(str, "cli.vol-") == 0 || strcmp(str, "vol-") == 0) {
      player.stepVol(false);
      return;
    }
    if (strcmp(str, "cli.vol+") == 0 || strcmp(str, "vol+") == 0) {
      player.stepVol(true);
      return;
    }
    if (strcmp(str, "sys.date") == 0 || strcmp(str, "date") == 0 || strcmp(str, "time") == 0) {
      network.requestTimeSync(true, clientId > MAX_TLN_CLIENTS?clientId:0);
      return;
    }
    int volume;
    if (sscanf(str, "vol(%d)", &volume) == 1 || sscanf(str, "cli.vol(\"%d\")", &volume) == 1 || sscanf(str, "vol %d", &volume) == 1) {
      if (volume < 0) volume = 0;
      if (volume > 254) volume = 254;
      player.setVol(volume);
      return;
    }
    if (strcmp(str, "cli.audioinfo") == 0 || strcmp(str, "audioinfo") == 0) {
      printf(clientId, "##CLI.AUDIOINFO#: %d\n> ", config.store.audioinfo > 0);
      return;
    }
    int ainfo;
    if (sscanf(str, "audioinfo(%d)", &ainfo) == 1 || sscanf(str, "cli.audioinfo(\"%d\")", &ainfo) == 1 || sscanf(str, "audioinfo %d", &ainfo) == 1) {
      config.saveValue(&config.store.audioinfo, ainfo > 0);
      printf(clientId, "new audioinfo value is: %d\n> ", config.store.audioinfo);
      return;
    }
    if (strcmp(str, "cli.smartstart") == 0 || strcmp(str, "smartstart") == 0) {
      printf(clientId, "##CLI.SMARTSTART#: %d\n> ", config.store.smartstart);
      return;
    }
    int sstart;
    if (sscanf(str, "smartstart(%d)", &sstart) == 1 || sscanf(str, "cli.smartstart(\"%d\")", &sstart) == 1 || sscanf(str, "smartstart %d", &sstart) == 1) {
      config.saveValue(&config.store.smartstart, static_cast<uint8_t>(sstart));
      printf(clientId, "new smartstart value is: %d\n> ", config.store.smartstart);
      return;
    }
    if (strcmp(str, "cli.list") == 0 || strcmp(str, "list") == 0) {
      printf(clientId, "#CLI.LIST#\n");
      File file = LittleFS.open(PLAYLIST_PATH, "r");
      if (!file || file.isDirectory()) {
        return;
      }
      char sName[BUFLEN], sUrl[BUFLEN];
      int sOvol;
      uint8_t c = 1;
      while (file.available()) {
        if (config.parseCSV(file.readStringUntil('\n').c_str(), sName, sUrl, sOvol)) {
          printf(clientId, "#CLI.LISTNUM#: %*d: %s, %s\n", 3, c, sName, sUrl);
          c++;
        }
      }
      printf(clientId, "##CLI.LIST#\n");
      printf(clientId, "> ");
      return;
    }
    if (strcmp(str, "cli.info") == 0 || strcmp(str, "info") == 0) {
      printf(clientId, "##CLI.INFO#\n");
      char timeStringBuff[50];
      strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &network.timeinfo);
      if (config.store.tzHour < 0) {
        printf(clientId, "##SYS.DATE#: %s-%02d:%02d\n", timeStringBuff, -(int)config.store.tzHour, config.store.tzMin);
      } else {
        printf(clientId, "##SYS.DATE#: %s+%02d:%02d\n", timeStringBuff, config.store.tzHour, config.store.tzMin);
      }
      printf(clientId, "##CLI.NAMESET#: %d %s\n", config.lastStation(), config.station.name);
      if (player.status() == PLAYING) {
        printf(clientId, "##CLI.META#: %s\n", config.station.title);
      }
      printf(clientId, "##CLI.VOL#: %d\n", player.audibleVolume());
      if (player.status() == PLAYING) {
        printf(clientId, "##CLI.PLAYING#\n");
      } else {
        printf(clientId, "##CLI.STOPPED#\n");
      }
      printf(clientId, "> ");
      return;
    }
    int sb;
    if (sscanf(str, "play(%d)", &sb) == 1 || sscanf(str, "cli.play(\"%d\")", &sb) == 1 || sscanf(str, "play %d", &sb) == 1 ) {
      if (sb < 1) sb = 1;
      if (sb >= config.store.countStation) sb = config.store.countStation;
      player.sendCommand({PR_PLAY, (uint16_t)sb});
      return;
    }
    #ifdef USE_SD
    int mm;
    if (sscanf(str, "mode %d", &mm) == 1 ) {
      if (mm > 2) mm = 0;
      if(mm==2)
        config.changeMode();
      else
        config.changeMode(mm);
      return;
    }
    #endif
    if (strcmp(str, "sys.tzo") == 0 || strcmp(str, "tzo") == 0) {
      printf(clientId, "##SYS.TZO#: %d:%d\n> ", config.store.tzHour, config.store.tzMin);
      return;
    }
    //int16_t tzh, tzm;
    int tzh, tzm;
    if (sscanf(str, "tzo(%d:%d)", &tzh, &tzm) == 2 || sscanf(str, "sys.tzo(\"%d:%d\")", &tzh, &tzm) == 2 || sscanf(str, "tzo %d:%d", &tzh, &tzm) == 2) {
      if (tzh < -12) tzh = -12;
      if (tzh > 14) tzh = 14;
      if (tzm < 0) tzm = 0;
      if (tzm > 59) tzm = 59;
      config.setTimezone((int8_t)tzh, (int8_t)tzm);
      if(tzh<0){
        printf(clientId, "new timezone offset: -%02d:%02d\n", -(int)config.store.tzHour, config.store.tzMin);
      }else{
        printf(clientId, "new timezone offset: %02d:%02d\n", config.store.tzHour, config.store.tzMin);
      }
      network.requestTimeSync(true);
      return;
    }
    if (sscanf(str, "tzo(%d)", &tzh) == 1 || sscanf(str, "sys.tzo(\"%d\")", &tzh) == 1 || sscanf(str, "tzo %d", &tzh) == 1) {
      if (tzh < -12) tzh = -12;
      if (tzh > 14) tzh = 14;
      config.setTimezone((int8_t)tzh, 0);
      if(tzh<0){
        printf(clientId, "new timezone offset: -%02d:%02d\n", -(int)config.store.tzHour, config.store.tzMin);
      }else{
        printf(clientId, "new timezone offset: %02d:%02d\n", config.store.tzHour, config.store.tzMin);
      }
      network.requestTimeSync(true);
      return;
    }
    if (sscanf(str, "dspon(%d)", &tzh) == 1 || sscanf(str, "cli.dspon(\"%d\")", &tzh) == 1 || sscanf(str, "dspon %d", &tzh) == 1) {
      config.setDspOn(tzh!=0);
      return;
    }
    if (sscanf(str, "dim(%d)", &tzh) == 1 || sscanf(str, "cli.dim(\"%d\")", &tzh) == 1 || sscanf(str, "dim %d", &tzh) == 1) {
      if (tzh < 0) tzh = 0;
      if (tzh > 100) tzh = 100;
      config.store.brightness = (uint8_t)tzh;
      config.setBrightness(true);
      return;
    }
    const ParsedLegacySleep legacy_sleep = parse_legacy_sleep(str);
    if (legacy_sleep.matched) {
      if (!legacy_sleep.valid) {
        printf(clientId, "##CMD_ERROR#\tunknown command <%s>\n> ", str);
        return;
      }
      const DeepSleepWakeRequest wake =
          DeepSleepWakeRequest::explicitMinutes(legacy_sleep.wake_minutes);
      const TimerCommandResult result =
          legacy_sleep.delayed
              ? timer_schedule_deep_sleep(legacy_sleep.after_minutes, wake)
              : timer_request_deep_sleep_now(wake);
      if (result == TimerCommandResult::Applied) {
        if (legacy_sleep.delayed) {
          printf(clientId, "sleep for %u minutes after %u minutes ...\n> ",
                 static_cast<unsigned>(legacy_sleep.wake_minutes),
                 static_cast<unsigned>(legacy_sleep.after_minutes));
        } else {
          printf(clientId, "sleep for %u minutes ...\n> ",
                 static_cast<unsigned>(legacy_sleep.wake_minutes));
        }
      } else {
        print_timer_failure(result);
      }
      return;
    }
  }
  if (strcmp(str, "sys.version") == 0 || strcmp(str, "version") == 0) {
    printf(clientId, "##SYS.VERSION#: %s\n> ", YOVERSION);
    return;
  }
  if (strcmp(str, "sys.boot") == 0 || strcmp(str, "boot") == 0 || strcmp(str, "reboot") == 0) {
    ESP.restart();
    return;
  }
  if (strcmp(str, "sys.reset") == 0 || strcmp(str, "reset") == 0) {
    config.reset();
    return;
  }
  if (strcmp(str, "wifi.list") == 0 || strcmp(str, "wifi") == 0) {
    printf(clientId, "#WIFI.SCAN#\n");
    int n = WiFi.scanNetworks();
    if (n == 0) {
        printf(clientId, "no networks found\n");
    } else {
      for (int i = 0; i < n; ++i) {
        printf(clientId, "%d", i + 1);
        printf(clientId, ": ");
        printf(clientId, "%s", WiFi.SSID(i));
        printf(clientId, " (");
        printf(clientId, "%d", WiFi.RSSI(i));
        printf(clientId, ")");
        printf(clientId, (WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" ":"*");
        printf(clientId, "\n");
        delay(10);
      }
    }
    printf(clientId, "#WIFI.SCAN#\n> ");
    return;
  }
  if (strcmp(str, "wifi.con") == 0 || strcmp(str, "conn") == 0) {
    printf(clientId, "#WIFI.CON#\n");
    File file = LittleFS.open(SSIDS_PATH, "r");
    if (file && !file.isDirectory()) {
      char sSid[BUFLEN], sPas[BUFLEN];
      uint8_t c = 1;
      while (file.available()) {
        if (config.parseSsid(file.readStringUntil('\n').c_str(), sSid, sPas)) {
          printf(clientId, "%d: %s, %s\n", c, sSid, sPas);
          c++;
        }
      }
    }
    printf(clientId, "##WIFI.CON#\n> ");
    return;
  }
  if (strcmp(str, "wifi.station") == 0 || strcmp(str, "station") == 0 || strcmp(str, "ssid") == 0) {
    printf(clientId, "#WIFI.STATION#\n");
    File file = LittleFS.open(SSIDS_PATH, "r");
    if (file && !file.isDirectory()) {
      char sSid[BUFLEN], sPas[BUFLEN];
      uint8_t c = 1;
      while (file.available()) {
        if (config.parseSsid(file.readStringUntil('\n').c_str(), sSid, sPas)) {
          if(c==config.store.lastSSID) printf(clientId, "%d: %s, %s\n", c, sSid, sPas);
          c++;
        }
      }
    }
    printf(clientId, "##WIFI.STATION#\n> ");
    return;
  }
  char newssid[30], newpass[40];
  if (sscanf(str, "wifi.con(\"%[^\"]\",\"%[^\"]\")", newssid, newpass) == 2 || sscanf(str, "wifi.con(%[^,],%[^)])", newssid, newpass) == 2 || sscanf(str, "wifi.con(%[^ ] %[^)])", newssid, newpass) == 2 || sscanf(str, "wifi %[^ ] %s", newssid, newpass) == 2) {
    char buf[BUFLEN];
    snprintf(buf, BUFLEN, "New SSID: \"%s\" with PASS: \"%s\" for next boot\n> ", newssid, newpass);
    printf(clientId, buf);
    printf(clientId, "...REBOOTING...\n> ");
    memset(buf, 0, BUFLEN);
    snprintf(buf, BUFLEN, "%s\t%s", newssid, newpass);
    config.saveWifiFromPost(buf);
    return;
  }
  if (strcmp(str, "wifi.status") == 0 || strcmp(str, "status") == 0) {
    printf(clientId, "#WIFI.STATUS#\nStatus:\t\t%d\nMode:\t\t%s\nIP:\t\t%s\nMask:\t\t%s\nGateway:\t%s\nRSSI:\t\t%d dBm\n##WIFI.STATUS#\n> ", 
      WiFi.status(), WiFi.getMode()==WIFI_STA?"WIFI_STA":"WIFI_AP", 
      WiFi.getMode()==WIFI_STA?WiFi.localIP().toString():WiFi.softAPIP().toString(),
      WiFi.getMode()==WIFI_STA?WiFi.subnetMask().toString():"255.255.255.0",
      WiFi.getMode()==WIFI_STA?WiFi.gatewayIP().toString():WiFi.softAPIP().toString(),
      WiFi.RSSI()
    );
    return;
  }
  if (strcmp(str, "wifi.rssi") == 0 || strcmp(str, "rssi") == 0) {
    printf(clientId, "#WIFI.RSSI#\t%d dBm\n> ", WiFi.RSSI());
    return;
  }
  if (strcmp(str, "sys.heap") == 0 || strcmp(str, "heap") == 0) {
    printf(clientId, "Free heap:\t%d bytes\n> ", xPortGetFreeHeapSize());
    return;
  }
#ifndef DUMMYDISPLAY
  // Block 8-E1: one-shot display topology / memory snapshot (no periodic logging).
  // Block 8-E1: однократный снимок дисплея / памяти (без периодических логов).
  // Short alias "dd" for interactive telnet (same handler as diag display).
  // Короткий алиас "dd" для интерактивного telnet (тот же handler, что diag display).
  if (strcmp(str, "diag display") == 0 || strcmp(str, "displaydiag") == 0 || strcmp(str, "dd") == 0) {
    static char s_diag_display_buf[1024];
    const size_t n = display.diagSnapshot(s_diag_display_buf, sizeof(s_diag_display_buf));
    printf(clientId, "##DIAG.DISPLAY#\n");
    if (n > 0) {
      printDiagBody(clientId, s_diag_display_buf);
    }
    printf(clientId, "##DIAG.DISPLAY#\n> ");
    return;
  }
#endif
  if (strcmp(str, "wifi.discon") == 0 || strcmp(str, "discon") == 0 || strcmp(str, "disconnect") == 0) {
    printf(clientId, "#WIFI.DISCON#\tdisconnected...\n> ");
    WiFi.disconnect();
    return;
  }
  telnet.printf(clientId, "##CMD_ERROR#\tunknown command <%s>\n> ", str);
}
