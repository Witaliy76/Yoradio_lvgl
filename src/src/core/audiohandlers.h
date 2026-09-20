#ifndef AUDIOHANDLERS_H
#define AUDIOHANDLERS_H

#include "../audioI2S/audio_text_url_utils.h"

//=============================================//
//              Audio handlers                 //
//=============================================//

void audio_info(const char *info) {
  if(!info) return;
  if(player.lockOutput) return;
  if(config.store.audioinfo) telnet.printf("##AUDIO.INFO#: %s\n", info);
  if (strstr(info, "format is mp3")  != NULL) { config.setBitrateFormat(BF_MP3); display.putRequest(DBITRATE); }
  if (strstr(info, "format is aac")  != NULL) { config.setBitrateFormat(BF_AAC); display.putRequest(DBITRATE); }
  if (strstr(info, "format is flac") != NULL) { config.setBitrateFormat(BF_FLAC); display.putRequest(DBITRATE); }
  if (strstr(info, "format is wav")  != NULL) { config.setBitrateFormat(BF_WAV); display.putRequest(DBITRATE); }
  if (strstr(info, "format is ogg")  != NULL) { config.setBitrateFormat(BF_OGG); display.putRequest(DBITRATE); }
  if (strstr(info, "format is vorbis")  != NULL) { config.setBitrateFormat(BF_VOR); display.putRequest(DBITRATE); }
  if (strstr(info, "format is opus")  != NULL) { config.setBitrateFormat(BF_OPU); display.putRequest(DBITRATE); }
  if (strstr(info, "skip metadata") != NULL) {
    player.setError("");
    config.setTitle(config.station.name);
  }
  if (strstr(info, "Account already in use") != NULL || strstr(info, "HTTP/1.0 401") != NULL) {
    player.setError(info);
    
  }
  const char* ici;
  char b[20] = {0};
  // Parsed stream facts for LVGL Main meta row (same strings as Audio::showCodecParams()).
  // Факты потока для Main meta — те же строки, что шлёт библиотека после decode.
  if ((ici = strstr(info, "SampleRate: ")) != NULL) {
    uint32_t sampleRate = 0;
    if(audio_safe::parseUnsignedDecimal(ici + 12, sampleRate)) config.station.stream_sample_rate_hz = sampleRate;
  }
  if ((ici = strstr(info, "BitsPerSample: ")) != NULL) {
    uint32_t bits = 0;
    if(audio_safe::parseUnsignedDecimal(ici + 15, bits)) {
      config.station.stream_bits_per_sample = static_cast<uint8_t>(bits > 255u ? 255u : bits);
    }
  }
  if ((ici = strstr(info, "BitRate: ")) != NULL) {
    strlcpy(b, ici + 9, sizeof(b));
    audio_bitrate(b);
  }
  if (((ici = strstr(info, "StreamTitle= ")) != NULL) /*&& strlen(info) > 15*/) {
    if(strlen(config.station.title)==0 || strcmp(config.station.title, config.station.name)==0 ||
       isTransientPlaybackTitle(config.station.title)){
      char streamTitle[BUFLEN] = {0};
      if(audio_safe::copyMetadataUtf8(streamTitle, sizeof(streamTitle), ici + 13) && streamTitle[0] != '\0') {
        audio_id3album(streamTitle);
      }
    }
  }
  if (((ici = strstr(info, "icy-name: ")) != NULL) && strlen(info) > 12) {
    if(strlen(config.station.title)==0 || strcmp(config.station.title, config.station.name)==0 ||
       isTransientPlaybackTitle(config.station.title)) {
      char icyName[BUFLEN] = {0};
      if(!audio_safe::copyMetadataUtf8(icyName, sizeof(icyName), ici + 10) || icyName[0] == '\0') return;
      #ifdef NAME_STRIM
         if ((ici = strstr(icyName, " - " )) != NULL) {
           char icySt[BUFLEN] = {0};
           const size_t stationLen = static_cast<size_t>(ici - icyName);
           memcpy(icySt, icyName, stationLen);
           icySt[stationLen] = '\0';
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
  if(!info) return;
  if(config.store.audioinfo) telnet.printf("%s %s\n", "##AUDIO.BITRATE#:", info);
  uint32_t br = 0;
  if(!audio_safe::parseUnsignedDecimal(info, br)) return;
  if (br > 3000) br = br / 1000;
  config.station.bitrate = static_cast<uint16_t>(br > 65535u ? 65535u : br);
  display.putRequest(DBITRATE);
  netserver.requestOnChange(BITRATE, 0);
}

bool printable(const char *info) {
  return audio_safe::isValidUtf8(info, true);
}

void audio_showstation(const char *info) {
  if(!info) return;
  char clean[BUFLEN] = {0};
  const bool p = audio_safe::copyMetadataUtf8(clean, sizeof(clean), info) && clean[0] != '\0';
  //config.setTitle(p?info:config.station.name);
  if(player.remoteStationName){
    if(p) config.setStation(clean);
    display.putRequest(NEWSTATION);
    netserver.requestOnChange(STATION, 0);
  }
}

void audio_showstreamtitle(const char *info) {
  DBGH();
  if(!info) return;
  if (strstr(info, "Account already in use") != NULL || strstr(info, "HTTP/1.0 401") != NULL) player.setError(info);
  char clean[BUFLEN] = {0};
  const bool p = audio_safe::copyMetadataUtf8(clean, sizeof(clean), info) && clean[0] != '\0';
  #ifdef DEBUG_TITLES
    player.setError("");
    config.setTitle(DEBUG_TITLES);
  #else
    if (p) {
      player.setError("");
      config.setTitle(clean);
    } else if (strlen(config.station.title)==0) {
      player.setError("");
      config.setTitle(config.station.name);
    }
  #endif
}

void audio_error(const char *info) {
  // E36AUD0A: setError owns ##ERROR# telnet; no title pipeline / telnet только в setError
  if(info) player.setError(info);
}

void audio_id3artist(const char *info){
  char clean[BUFLEN] = {0};
  if(audio_safe::copyMetadataUtf8(clean, sizeof(clean), info)) config.setStation(clean);
  display.putRequest(NEWSTATION);
  netserver.requestOnChange(STATION, 0);
}

void audio_id3album(const char *info){
  if(!info) return;
  if(player.lockOutput) return;
  char clean[BUFLEN] = {0};
  if(audio_safe::copyMetadataUtf8(clean, sizeof(clean), info)){
    player.setError("");
    if(strlen(config.station.title)==0 || strcmp(config.station.title, config.station.name)==0 ||
       isTransientPlaybackTitle(config.station.title)){
      config.setTitle(clean);
    }else{
      char out[BUFLEN]= {0};
      strlcat(out, config.station.title, BUFLEN);
      strlcat(out, " - ", BUFLEN);
      strlcat(out, clean, BUFLEN);
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
