#ifndef AUDIOHANDLERS_H
#define AUDIOHANDLERS_H

//=============================================//
//              Audio handlers                 //
//=============================================//

void audio_info(const char *info) {
  if(player.lockOutput) return;
  if(config.store.audioinfo) telnet.printf("##AUDIO.INFO#: %s\n", info);
  if (strstr(info, "format is mp3")  != NULL) { config.setBitrateFormat(BF_MP3); display.putRequest(DBITRATE); }
  if (strstr(info, "format is aac")  != NULL) { config.setBitrateFormat(BF_AAC); display.putRequest(DBITRATE); }
  if (strstr(info, "format is flac") != NULL) { config.setBitrateFormat(BF_FLAC); display.putRequest(DBITRATE); }
  if (strstr(info, "format is wav")  != NULL) { config.setBitrateFormat(BF_WAV); display.putRequest(DBITRATE); }
  if (strstr(info, "format is ogg")  != NULL) { config.setBitrateFormat(BF_OGG); display.putRequest(DBITRATE); }
  if (strstr(info, "format is vorbis")  != NULL) { config.setBitrateFormat(BF_VOR); display.putRequest(DBITRATE); }
  if (strstr(info, "format is opus")  != NULL) { config.setBitrateFormat(BF_OPU); display.putRequest(DBITRATE); }
  if (strstr(info, "skip metadata") != NULL) config.setTitle(config.station.name);
  if (strstr(info, "Account already in use") != NULL || strstr(info, "HTTP/1.0 401") != NULL) {
    player.setError(info);
    
  }
  char* ici; char b[20]={0};
  // Parsed stream facts for LVGL Main meta row (same strings as Audio::showCodecParams()).
  // Факты потока для Main meta — те же строки, что шлёт библиотека после decode.
  if ((ici = strstr(info, "SampleRate: ")) != NULL) {
    config.station.stream_sample_rate_hz = static_cast<uint32_t>(atoi(ici + 12));
  }
  if ((ici = strstr(info, "BitsPerSample: ")) != NULL) {
    config.station.stream_bits_per_sample = static_cast<uint8_t>(atoi(ici + 15));
  }
  if ((ici = strstr(info, "BitRate: ")) != NULL) {
    strlcpy(b, ici + 9, 50);
    audio_bitrate(b);
  }
  if (((ici = strstr(info, "StreamTitle= ")) != NULL) /*&& strlen(info) > 15*/) {
    if(strlen(config.station.title)==0 || strcmp(config.station.title, config.station.name)==0 || strstr(config.station.title, "timeout") != NULL ||
       strstr(config.station.title, "[соединение]") != NULL || strstr(config.station.title, "[connecting]") != NULL ||
       strstr(config.station.title, "(connection)") != NULL || strstr(config.station.title, "[ready]") != NULL ||
       strstr(config.station.title, "[готов]") != NULL || strstr(config.station.title, "[stopped]") != NULL ||
       strstr(config.station.title, "[остановлено]") != NULL){
      char StreamTitle[strlen(info)+1]={0};
      strlcpy(StreamTitle, ici + 13, strlen(info));
      audio_id3album(StreamTitle);
    }
  }
  if (((ici = strstr(info, "icy-name: ")) != NULL) && strlen(info) > 12) {
    if(strlen(config.station.title)==0 || strcmp(config.station.title, config.station.name)==0 || strstr(config.station.title, "timeout") != NULL ||
       strstr(config.station.title, "[соединение]") != NULL || strstr(config.station.title, "[connecting]") != NULL ||
       strstr(config.station.title, "(connection)") != NULL || strstr(config.station.title, "[ready]") != NULL ||
       strstr(config.station.title, "[готов]") != NULL || strstr(config.station.title, "[stopped]") != NULL ||
       strstr(config.station.title, "[остановлено]") != NULL) {
      char icyName[strlen(info)+1]={0};
      strlcpy(icyName, ici + 10, strlen(info));
      #ifdef NAME_STRIM
         if ((ici = strstr(icyName, " - " )) != NULL) {
           char icySt[strlen(icyName) - strlen(ici) + 1]={0};
           strlcpy(icySt, icyName, strlen(icyName) - strlen(ici) + 1);
           config.setStation(icySt);
         } else
           config.setStation(icyName);
         display.putRequest(NEWSTATION);
         netserver.requestOnChange(STATION, 0);
     #endif
         audio_id3album(icyName);
    }
  }
}

void audio_bitrate(const char *info)
{
  if(config.store.audioinfo) telnet.printf("%s %s\n", "##AUDIO.BITRATE#:", info);
  uint32_t br = atoi(info);
  if (br > 3000) br = br / 1000;
  config.station.bitrate = br;
  display.putRequest(DBITRATE);
  netserver.requestOnChange(BITRATE, 0);
}

bool printable(const char *info) {
  if(L10N_LANGUAGE!=RU) return true;
  bool p = true;
  for (int c = 0; c < strlen(info); c++)
  {
    if ((uint8_t)info[c] > 0x7e || (uint8_t)info[c] < 0x20) p = false;
  }
  if (!p) p = (uint8_t)info[0] >= 0xC2 && (uint8_t)info[1] >= 0x80 && (uint8_t)info[1] <= 0xBF;
  return p;
}

void audio_showstation(const char *info) {
  bool p = printable(info) && (strlen(info) > 0);(void)p;
  //config.setTitle(p?info:config.station.name);
  if(player.remoteStationName){
    config.setStation(p?info:config.station.name);
    display.putRequest(NEWSTATION);
    netserver.requestOnChange(STATION, 0);
  }
}

void audio_showstreamtitle(const char *info) {
  DBGH();
  if (strstr(info, "Account already in use") != NULL || strstr(info, "HTTP/1.0 401") != NULL) player.setError(info);
  bool p = (strlen(info) > 0) && printable(info);
  #ifdef DEBUG_TITLES
    config.setTitle(DEBUG_TITLES);
  #else
    if (p) config.setTitle(info);
    else if (strlen(config.station.title)==0) config.setTitle(config.station.name);
  #endif
}

void audio_error(const char *info) {
  //config.setTitle(info);
  player.setError(info);
  telnet.printf("##ERROR#:\t%s\n", info);
}

void audio_id3artist(const char *info){
  if(printable(info)) config.setStation(info);
  display.putRequest(NEWSTATION);
  netserver.requestOnChange(STATION, 0);
}

void audio_id3album(const char *info){
  if(player.lockOutput) return;
  if(printable(info)){
    if(strlen(config.station.title)==0 || strcmp(config.station.title, config.station.name)==0 || strstr(config.station.title, "timeout") != NULL ||
       strstr(config.station.title, "[соединение]") != NULL || strstr(config.station.title, "[connecting]") != NULL ||
       strstr(config.station.title, "(connection)") != NULL || strstr(config.station.title, "[ready]") != NULL ||
       strstr(config.station.title, "[готов]") != NULL || strstr(config.station.title, "[stopped]") != NULL ||
       strstr(config.station.title, "[остановлено]") != NULL){
      config.setTitle(info);
    }else{
      char out[BUFLEN]= {0};
      strlcat(out, config.station.title, BUFLEN);
      strlcat(out, " - ", BUFLEN);
      strlcat(out, info, BUFLEN);
      config.setTitle(out);
    }
  }
}

void audio_id3title(const char *info){
  audio_id3album(info);
}

void audio_beginSDread(){
  config.setTitle("");
}

void audio_id3data(const char *info){  //id3 metadata
    if(player.lockOutput) return;
    telnet.printf("##AUDIO.ID3#: %s\n", info);
}

void audio_eof_mp3(const char *info){  //end of file
    config.sdResumePos = 0;
    player.next();
}

void audio_eof_stream(const char *info){
  player.sendCommand({PR_STOP, 0});
  if(!player.resumeAfterUrl) return;
  if (config.getMode()==PM_WEB){
    player.sendCommand({PR_PLAY, config.lastStation()});
  }else{
    player.setResumeFilePos( config.sdResumePos==0?0:config.sdResumePos-player.sd_min);
    player.sendCommand({PR_PLAY, config.lastStation()});
  }
}

void audio_progress(uint32_t startpos, uint32_t endpos){
  player.sd_min = startpos;
  player.sd_max = endpos;
  netserver.requestOnChange(SDLEN, 0);
}

#endif
