#if SDC_CS!=255

#define USE_SD
#include "sdmanager.h"
#include "display.h"
#include "player.h"

#if defined(SD_SPIPINS) || SD_HSPI
SPIClass  SDSPI(HOOPSENb);
#define SDREALSPI SDSPI
#else
  #define SDREALSPI SPI
#endif

#ifndef SDSPISPEED
  #define SDSPISPEED 20000000
#endif

SDManager sdman(FSImplPtr(new VFSImpl()));

bool SDManager::start(){
  // ПРОВЕРКА ПАМЯТИ ПЕРЕД ИНИЦИАЛИЗАЦИЕЙ SD
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 100000) {  // Только критические уровни памяти
    Serial.printf("##[CRITICAL]# SDManager: CRITICAL MEMORY before SD init! Only %u bytes free\n", freeHeap);
    heap_caps_check_integrity_all(true);
    delay(10);
    freeHeap = ESP.getFreeHeap();
    Serial.printf("##[DEBUG]# SDManager: After cleanup: %u bytes free\n", freeHeap);
  }
  
  if(xSemaphoreTake(sdMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  
  // Явная инициализация SPI-шины и подготовка CS
  if (SDC_CS != 255) {
    pinMode(SDC_CS, OUTPUT);
    digitalWrite(SDC_CS, HIGH); // снять выбор устройства на время настройки SPI
  }

#ifdef SD_SPIPINS
  // Инициализируем шину с указанными пинами (SCK, MISO, MOSI)
  SDREALSPI.begin(SD_SPIPINS);
#elif SD_HSPI
  // Инициализация шины по умолчанию для выбранного хоста
  SDREALSPI.begin();
#endif

  ready = begin(SDC_CS, SDREALSPI, SDSPISPEED);
  vTaskDelay(10);
  if(!ready) ready = begin(SDC_CS, SDREALSPI, SDSPISPEED);
  vTaskDelay(10);
  if(!ready) ready = begin(SDC_CS, SDREALSPI, SDSPISPEED);
  
  xSemaphoreGive(sdMutex);
  return ready;
}

void SDManager::stop(){
  if(xSemaphoreTake(sdMutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  
  end();
  ready = false;
  
  xSemaphoreGive(sdMutex);
}

#include "diskio_impl.h"
bool SDManager::cardPresent() {
  // ПРОВЕРКА ПАМЯТИ ПЕРЕД ПРОВЕРКОЙ SD
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 100000) {  // Только критические уровни памяти
    Serial.printf("##[CRITICAL]# SDManager: CRITICAL MEMORY in cardPresent! Only %u bytes free\n", freeHeap);
    heap_caps_check_integrity_all(true);
    delay(5);
  }
  
  if(xSemaphoreTake(sdMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if(!ready) {
    xSemaphoreGive(sdMutex);
    return false;
  }
  
  if(sectorSize()<1) {
    xSemaphoreGive(sdMutex);
    return false;
  }
  
  uint8_t buff[sectorSize()] = { 0 };
  bool bread = readRAW(buff, 1);
  
  xSemaphoreGive(sdMutex);
  if(sectorSize()>0 && !bread) return false;
  return bread;
}

bool SDManager::_checkNoMedia(const char* path, bool mutexAlreadyTaken){
  bool mutexTakenHere = false;
  
  // Захватываем mutex только если он еще не захвачен
  if (!mutexAlreadyTaken) {
    if(xSemaphoreTake(sdMutex, portMAX_DELAY) != pdTRUE) {
      return false;
    }
    mutexTakenHere = true;
  }
  
  // Основная логика проверки .nomedia файла
  char nomedia[BUFLEN]= {0};
  strlcat(nomedia, path, BUFLEN);
  strlcat(nomedia, "/.nomedia", BUFLEN);
  bool nm = exists(nomedia);
  
  // Освобождаем mutex только если мы его захватывали
  if (mutexTakenHere) {
    xSemaphoreGive(sdMutex);
  }
  
  return nm;
}

bool SDManager::_endsWith (const char* base, const char* str) {
  int slen = strlen(str) - 1;
  const char *p = base + strlen(base) - 1;
  while(p > base && isspace(*p)) p--;
  p -= slen;
  if (p < base) return false;
  return (strncmp(p, str, slen) == 0);
}

void SDManager::listSD(File &plSDfile, File &plSDindex, const char* dirname, uint8_t levels) {
    // ПРОВЕРКА ПАМЯТИ ПЕРЕД ОТКРЫТИЕМ SD
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 100000) {  // Только критические уровни памяти
        Serial.printf("##[CRITICAL]# SDManager: CRITICAL MEMORY! Only %u bytes free\n", freeHeap);
        // Принудительная очистка памяти
        heap_caps_check_integrity_all(true);
        delay(5);
        
        // ДОПОЛНИТЕЛЬНАЯ ОЧИСТКА ПАМЯТИ
        if (freeHeap < 80000) {  // Экстренная ситуация
            Serial.printf("##[EMERGENCY]# SDManager: EMERGENCY MEMORY CLEANUP!\n");
            // Множественная очистка heap
            for (int i = 0; i < 3; i++) {
                heap_caps_check_integrity_all(true);
                delay(5);
                ESP.getFreeHeap();  // Триггер сборки мусора
                delay(3);
            }
            Serial.printf("##[DEBUG]# SDManager: Emergency cleanup completed\n");
        }
        
        freeHeap = ESP.getFreeHeap();
        Serial.printf("##[DEBUG]# SDManager: After cleanup: %u bytes free\n", freeHeap);
    }
    
    if(xSemaphoreTake(sdMutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    
    File root = sdman.open(dirname);
    if (!root) {
        Serial.println("##[ERROR]#\tFailed to open directory");
        xSemaphoreGive(sdMutex);
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("##[ERROR]#\tNot a directory");
        xSemaphoreGive(sdMutex);
        return;
    }

    uint32_t pos = 0;
    char* filePath;
    while (true) {
        vTaskDelay(2);
        player.loop();
        bool isDir;
        String fileName = root.getNextFileName(&isDir);
        if (fileName.isEmpty()) break;
        filePath = (char*)malloc(fileName.length() + 1);
        if (filePath == NULL) {
            Serial.println("Memory allocation failed");
            break;
        }
        strcpy(filePath, fileName.c_str());
        const char* fn = strrchr(filePath, '/') + 1;
        if (isDir) {
            if (levels && !_checkNoMedia(filePath, true)) { // Передаем true - mutex уже захвачен
                xSemaphoreGive(sdMutex);  // Освобождаем перед рекурсией
                listSD(plSDfile, plSDindex, filePath, levels - 1);
                if(xSemaphoreTake(sdMutex, portMAX_DELAY) != pdTRUE) {
                    free(filePath);
                    root.close();
                    return;
                }
            }
        } else {
            if (_endsWith(strlwr((char*)fn), ".mp3") || _endsWith(fn, ".m4a") || _endsWith(fn, ".aac") ||
                _endsWith(fn, ".wav") || _endsWith(fn, ".flac")) {
                pos = plSDfile.position();
                plSDfile.printf("%s\t%s\t0\n", fn, filePath);
                plSDindex.write((uint8_t*)&pos, 4);
                Serial.print(".");
                // Block 8-E8: SDFILEINDEX was legacy SDCHANGE progress only — removed.
                _sdFCount++;
                if (_sdFCount % 64 == 0) Serial.println();
            }
        }
        free(filePath);
    }
    root.close();
    xSemaphoreGive(sdMutex);
}

void SDManager::indexSDPlaylist() {
    if(xSemaphoreTake(sdMutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    
    _sdFCount = 0;
    if(exists(PLAYLIST_SD_PATH)) remove(PLAYLIST_SD_PATH);
    if(exists(INDEX_SD_PATH)) remove(INDEX_SD_PATH);
    File playlist = open(PLAYLIST_SD_PATH, "w", true);
    if (!playlist) {
        xSemaphoreGive(sdMutex);
        return;
    }
    File index = open(INDEX_SD_PATH, "w", true);
    
    xSemaphoreGive(sdMutex);  // Освобождаем перед длительной операцией
    
    listSD(playlist, index, "/", SD_MAX_LEVELS);
    
    if(xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        index.flush();
        index.close();
        playlist.flush();
        playlist.close();
        xSemaphoreGive(sdMutex);
    }
    
    Serial.println();
    delay(50);
}
#endif


