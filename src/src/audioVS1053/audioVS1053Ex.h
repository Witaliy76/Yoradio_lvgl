/*
 *  vs1053_ext.h
 *
 *  Created on: Jul 09.2017
 *  Updated on: Jul 18.2025 (Maleksm)
 *      Author: Wolle (schreibfaul1), easy
 */
#if VS1053_CS!=255
#ifndef _vs1053_ext
#define _vs1053_ext

#pragma once
//#pragma GCC optimize ("Ofast")
#include "esp_arduino_version.h"
#include <vector>
#include <Arduino.h>
#include <libb64/cencode.h>
//#include <esp32-hal-log.h>
//#include "SPI.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <codecvt>
#include <locale>
#include <memory>
#include "../audioI2S/psram_unique_ptr.hpp"

#include <SD.h>
#include <SD_MMC.h>
#include <LittleFS.h>
#include <FS.h>
#include <FFat.h>

#if ESP_IDF_VERSION_MAJOR >= 5
    #include "driver/gpio.h"
#endif

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#include "hal/gpio_ll.h"
#endif

#include "vs1053b-patches-flac.h"

#define VS1053VOLM 128				// 128 or 96 only
#define VS1053VOL(v) (VS1053VOLM==128?log10(((float)v+1)) * 50.54571334 + 128:log10(((float)v+1)) * 64.54571334 + 96)

extern __attribute__((weak)) void audio_info(const char*);
extern __attribute__((weak)) void audio_id3data(const char*); //ID3 metadata
extern __attribute__((weak)) void audio_id3image(File& file, const size_t pos, const size_t size); //ID3 metadata image
extern __attribute__((weak)) void audio_oggimage(File& file, std::vector<uint32_t> v); //OGG blockpicture
extern __attribute__((weak)) void audio_id3lyrics(File& file, const size_t pos, const size_t size); //ID3 metadata lyrics
extern __attribute__((weak)) void audio_eof_mp3(const char*); //end of mp3 file
extern __attribute__((weak)) void audio_showstreamtitle(const char*);
extern __attribute__((weak)) void audio_showstation(const char*);
extern __attribute__((weak)) void audio_bitrate(const char*);
extern __attribute__((weak)) void audio_commercial(const char*);
extern __attribute__((weak)) void audio_icyurl(const char*);
extern __attribute__((weak)) void audio_icylogo(const char*);
extern __attribute__((weak)) void audio_icydescription(const char*);
extern __attribute__((weak)) void audio_lasthost(const char*);
extern __attribute__((weak)) void audio_eof_speech(const char*);
extern __attribute__((weak)) void audio_eof_stream(const char*); // The webstream comes to an end
extern __attribute__((weak)) void audio_log(uint8_t logLevel, const char* msg, const char* arg);

extern __attribute__((weak)) void audio_id3artist(const char*);
extern __attribute__((weak)) void audio_id3album(const char*);
extern __attribute__((weak)) void audio_id3title(const char*);
extern __attribute__((weak)) void audio_beginSDread();
extern __attribute__((weak)) void audio_progress(uint32_t startpos, uint32_t endpos);
extern __attribute__((weak)) void audio_error(const char*);


//----------------------------------------------------------------------------------------------------------------------

class AudioBuffer {
// AudioBuffer will be allocated in PSRAM, If PSRAM not available or has not enough space AudioBuffer will be
// allocated in FlashRAM with reduced size
//
//  m_buffer            m_readPtr                 m_writePtr                 m_endPtr
//   |                       |<------dataLength------->|<------ writeSpace ----->|
//   ▼                       ▼                         ▼                         ▼
//   ---------------------------------------------------------------------------------------------------------------
//   |                     <--m_buffSize-->                                      |      <--m_resBuffSize -->     |
//   ---------------------------------------------------------------------------------------------------------------
//   |<-----freeSpace------->|                         |<------freeSpace-------->|
//
//
//
//   if the space between m_readPtr and buffend < m_resBuffSize copy data from the beginning to resBuff
//   so that the mp3/aac/flac frame is always completed
//
//  m_buffer                      m_writePtr                 m_readPtr        m_endPtr
//   |                                 |<-------writeSpace------>|<--dataLength-->|
//   ▼                                 ▼                         ▼                ▼
//   ---------------------------------------------------------------------------------------------------------------
//   |                        <--m_buffSize-->                                    |      <--m_resBuffSize -->     |
//   ---------------------------------------------------------------------------------------------------------------
//   |<---  ------dataLength--  ------>|<-------freeSpace------->|
//
//

public:
    AudioBuffer(size_t maxBlockSize = 0);       // constructor
    ~AudioBuffer();                             // frees the buffer
    size_t   init();                            // set default values
    bool     isInitialized() { return m_f_init; };
    int32_t  getBufsize();
    bool     setBufsize(size_t mbs);            // default is m_buffSizePSRAM for psram, and m_buffSizeRAM without psram
//    void     setBufsize(int ram, int psram);            // default is m_buffSizePSRAM for psram, and m_buffSizeRAM without psram
    void     changeMaxBlockSize(uint16_t mbs);  // is default 1600 for mp3 and aac, set 16384 for FLAC
    uint16_t getMaxBlockSize();                 // returns maxBlockSize
    size_t   freeSpace();                       // number of free bytes to overwrite
    size_t   writeSpace();                      // space fom writepointer to bufferend
    size_t   bufferFilled();                    // returns the number of filled bytes
    size_t   getMaxAvailableBytes();            // max readable bytes in one block
    void     bytesWritten(size_t bw);           // update writepointer
    void     bytesWasRead(size_t br);           // update readpointer
    uint8_t* getWritePtr();                     // returns the current writepointer
    uint8_t* getReadPtr();                      // returns the current readpointer
    uint32_t getWritePos();                     // write position relative to the beginning
    uint32_t getReadPos();                      // read position relative to the beginning
    void     resetBuffer();                     // restore defaults

protected:
    size_t            m_buffSize         = UINT16_MAX * 10;   // most webstreams limit the advance to 100...300Kbytes
    size_t            m_freeSpace        = 0;
    size_t            m_writeSpace       = 0;
    size_t            m_dataLength       = 0;
    size_t            m_resBuffSize      = 4096 * 6; // reserved buffspace, >= one flac frame
    size_t            m_maxBlockSize     = 1600;
    uint8_t*          m_buffer           = NULL;
    uint8_t*          m_writePtr         = NULL;
    uint8_t*          m_readPtr          = NULL;
    uint8_t*          m_endPtr           = NULL;
    bool              m_f_init           = false;
    bool              m_f_isEmpty        = true;
};

//----------------------------------------------------------------------------------------------------------------------

static const size_t AUDIO_STACK_SIZE = 3300;
static StaticTask_t __attribute__((unused)) xAudioTaskBuffer;
static StackType_t  __attribute__((unused)) xAudioStack[AUDIO_STACK_SIZE];
//extern char audioI2SVers[];

#ifndef YORADIO_AUDIO_TERMINAL_REASON_DEFINED
#define YORADIO_AUDIO_TERMINAL_REASON_DEFINED
// Shared Player contract; VS1053 has no internal webstream reconnect owner. /
// Общий контракт Player; у VS1053 нет внутреннего владельца webstream reconnect.
enum class AudioTerminalReason : uint8_t {
    NONE = 0,
    HEADER_RETRY_EXHAUSTED,
    UNSTABLE_STREAM_EXHAUSTED
};
#endif

class Audio{

    AudioBuffer InBuff; // instance of input buffer

private:
    typedef struct _ID3Hdr{ // used only in readID3header()
        size_t       id3Size;
        size_t       totalId3Size; // if we have more header, id3_1_size + id3_2_size + ....
        size_t       remainingHeaderBytes;
        size_t       universal_tmp;
        uint8_t      ID3version;
        int          ehsz;
        char         tag[5];
        char         frameid[5];
        char         lang[5];
        size_t       framesize;
        bool         compressed;
        size_t       APIC_size[3];
        uint32_t     APIC_pos[3];
        bool         SYLT_seen;
        size_t       SYLT_size;
        uint32_t     SYLT_pos;
        uint8_t      numID3Header;
        uint16_t     iBuffSize;
        uint8_t      contentDescriptorTerminator_0;
        uint8_t      contentDescriptorTerminator_1;
        uint8_t      textStringTerminator_0;
        uint8_t      textStringTerminator_1;
        bool         byteOrderMark;
        ps_ptr<char> iBuff;
    } ID3Hdr_t;
    ID3Hdr_t m_ID3Hdr;

    typedef struct _pwsHLS{  // used in processWebStreamHLS()
        uint16_t     maxFrameSize;
        uint16_t     ID3BuffSize;
        uint32_t     availableBytes;
        bool         firstBytes;
        bool         f_chunkFinished;
        uint32_t     byteCounter;
        size_t       chunkSize;
        uint16_t     ID3WritePtr;
        uint16_t     ID3ReadPtr;
        ps_ptr<uint8_t>ID3Buff;
    } pwsHLS_t;
    pwsHLS_t m_pwsHLS;

    typedef struct _pplM3U8{ // used in parsePlaylist_M3U8
        uint64_t     xMedSeq;
        bool         f_mediaSeq_found;
    } pplM3u8_t;
    pplM3u8_t m_pplM3U8;

    typedef struct _m4aHdr{ // used in read_M4A_Header
        size_t      headerSize;
        size_t      retvalue;
        size_t      atomsize;
        size_t      audioDataPos;
        size_t      cnt;
        uint32_t    picPos;
        uint32_t    picLen;
    } m4aHdr_t;
    m4aHdr_t m_m4aHdr;

    typedef struct _plCh{ // used in playChunk
        int32_t     validSamples;
        int32_t     samples48K = 0;
        uint32_t    count = 0;
        size_t      i2s_bytesConsumed;
        int16_t*    sample[2];
        int16_t*    s2;
        int         sampleSize;
        esp_err_t   err;
        int         i;
    } plCh_t;
    plCh_t m_plCh;

    typedef struct _lVar{ // used in loop
        uint8_t     no_host_cnt;
        uint32_t    no_host_timer;
        uint8_t     count;
    } lVar_t;
    lVar_t m_lVar;

    typedef struct _hwoe{ // used in dismantle_host
        bool ssl;
        ps_ptr<char> hwoe;  // host without extension
        uint16_t     port;
        ps_ptr<char> extension;
        ps_ptr<char> query_string;
    } hwoe_t;

    typedef struct _prlf{ // used in processLocalFile
        uint32_t  ctime;
        uint32_t  newFilePos;
        uint32_t  byteCounter;
        bool      audioHeaderFound;
        uint32_t  timeout;
        uint32_t  maxFrameSize;
        uint32_t  availableBytes;
        int32_t   bytesAddedToBuffer;
        int32_t   offset;
    } prlf_t;
    prlf_t m_prlf;

/*    typedef struct _cat { // used in computeAudioTime
        uint64_t sumBytesIn;
        uint64_t sumBytesOut;
        uint32_t sumBitRate;
        uint32_t counter;
        uint32_t timeStamp;
        uint32_t deltaBytesIn;
        uint32_t nominalBitRate;
    } cat_t;
    cat_t m_cat;
*/
    typedef struct _cVUl{ // used in computeVUlevel
        uint8_t sampleArray[2][4][8] = {0};
        uint8_t cnt0 = 0, cnt1 = 0, cnt2 = 0, cnt3 = 0, cnt4 = 0;
        bool    f_vu = false;
    } cVUl_t;
    cVUl_t m_cVUl;

    typedef struct _ifCh{ // used in IIR_filterChain0, 1, 2
        float   inSample0[2];
        float   outSample0[2];
        int16_t iir_out0[2];
        float   inSample1[2];
        float   outSample1[2];
        int16_t iir_out1[2];
        float   inSample2[2];
        float   outSample2[2];
        int16_t iir_out2[2];
    } ifCh_t;
    ifCh_t m_ifCh;

    typedef struct _tspp{ // used in ts_parsePacket
        int pidNumber = 0;
        int pids[4]; // PID_ARRAY_LEN
        int PES_DataLength = 0;
        int pidOfAAC = 0;
        uint8_t fillData = 0;
    } tspp_t;
    tspp_t m_tspp;

    typedef struct _pwst{ // used in processWebStream
        uint16_t  maxFrameSize;
        uint32_t  chunkSize =0;
        bool      f_skipCRLF = false;
        uint32_t  availableBytes;
        bool      f_clientIsConnected;
    } pwst_t;
    pwst_t m_pwst;

    typedef struct _pwf{
        uint32_t maxFrameSize;
        uint32_t chunkSize;
        size_t   audioDataCount;
        uint32_t byteCounter;
        bool     f_waitingForPayload = false;
        bool     f_clientIsConnected;
    } pwf_t;
    pwf_t m_pwf;

    typedef struct  _pad{ // used in playAudioData
        uint8_t  count = 0;
        size_t   oldAudioDataSize = 0;
        bool     lastFrames = false;
        int32_t  bytesToDecode;
        int16_t  bytesDecoded;
    } pad_t;
    pad_t m_pad;

    typedef struct _sbyt{ // used in sendBytes
        int32_t  bytesLeft;
        bool     f_setDecodeParamsOnce = true;
        uint8_t  channels = 0;
        int      nextSync = 0;
        uint8_t  isPS = 0;
    } sbyt_t;
    sbyt_t m_sbyt;

    typedef struct _rmet{ // used in readMetadata
        uint16_t pos_ml = 0; // determines the current position in metaline
        uint16_t metalen = 0;
        uint16_t res = 0;
    } rmet_t;
    rmet_t m_rmet;

    typedef struct _pwsts {                // used in processWebStreamTS
        uint32_t       availableBytes;     // available bytes in stream
        bool           f_firstPacket;
        bool           f_chunkFinished;
        bool           f_nextRound;
        uint32_t       byteCounter;        // count received data
        uint8_t        ts_packetStart = 0;
        uint8_t        ts_packetLength = 0;
        uint8_t        ts_packetPtr = 0;
        const uint8_t  ts_packetsize = 188;
        ps_ptr<uint8_t> ts_packet;
        size_t         chunkSize = 0;
    } pwsts_t;
    pwsts_t m_pwsst;

    typedef struct _rwh { // used in read_WAV_Header
        size_t   headerSize;
        uint32_t cs = 0;
        uint8_t  bts = 0;
    } rwh_t;
    rwh_t m_rwh;

    typedef struct _rflh { // used in read_FLAC_Header
        size_t   headerSize;
        size_t   retvalue = 0;
        bool     f_lastMetaBlock = false;
        uint32_t picPos = 0;
        uint32_t picLen = 0;
    } rflh_t;
    rflh_t m_rflh;

    typedef struct _phrh{ // used in parseHttpResponseHeader
        uint32_t ctime;
        uint32_t timeout;
        uint32_t stime;
        bool     f_time = false;
    } phrh_t;
    phrh_t m_phrh;

    typedef struct _sdet{ // used in streamDetection
        uint32_t tmr_slow = 0;
        uint32_t tmr_lost = 0;
        uint8_t  cnt_slow = 0;
        uint8_t  cnt_lost = 0;
    }sdet_t;
    sdet_t m_sdet;

    typedef struct _fnsy{ // used in findNextSync
        int      nextSync = 0;
        uint32_t swnf = 0;
    } fnsy_t;
    fnsy_t m_fnsy;

public:

    // Constructor.  Only sets pin values.  Doesn't touch the chip.  Be sure to call begin()!
#ifdef ARDUINO_ESP32S3_DEV
//    Audio(uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin, uint8_t spi = FSPI, uint8_t mosi = 11, uint8_t miso = 13, uint8_t sclk = 12);
    Audio(uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin, SPIClass *spi=&SPI);
#else
    Audio (uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin, SPIClass *spi=&SPI);
/*
  #if VS_HSPI
    Audio ( uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin, uint8_t spi=HSPI, uint8_t mosi = 11, uint8_t miso = 13, uint8_t sclk = 12);
  #else
    Audio ( uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin, uint8_t spi = VSPI, uint8_t mosi = 23, uint8_t miso = 19, uint8_t sclk = 18);
  #endif
*/
#endif

    ~Audio();

    void     begin() ;                                // Begin operation.  Sets pins correctly and prepares SPI bus.
    void     setVolume(uint8_t vol);            // Set the player volume.Level from 0-255, higher is louder.
    void     setBalance(int8_t bal = 0);	 // Adjusting the left and right volume balance, higher - right, lower - left side.
    void     setTone(int8_t* rtone);           // Set the player baas/treble, 4 nibbles for treble gain/freq and bass gain/freq
    void     setTone(int8_t gainLowPass, int8_t gainBandPass, int8_t gainHighPass);
    uint8_t  getVolume();                              // Get the current volume setting, higher is louder.
    void     printDetails(const char* str);           // Print configuration details to serial output.
    uint8_t  printVersion();                            // Returns version of vs1053 chip
    uint32_t printChipID();                            // Returns chipID of vs1053 chip
//   uint32_t getBitRate();                              // average br from WRAM register
   bool   setBitrate(uint32_t br);
//    void     softReset() ;                              // Do a soft reset
    /* VU METER */
    void     setVUmeter();
    uint16_t get_VUlevel(uint16_t dimension);
    void     computeVUlevel();
    bool     eofHeader;
    void     setDefaults(); 						// free buffers and set defaults
    void     beginPlaybackSession() {}
    AudioTerminalReason consumeTerminalReason() { return AudioTerminalReason::NONE; }
    bool     isWebstreamReconnectPending() const { return false; }
    void     cancelWebstreamReconnect(const char*) {}
//    void     loadUserCode();

    bool openai_speech(const String& api_key, const String& model, const String& input, const String& instructions, const String& voice, const String& response_format, const String& speed);
    hwoe_t dismantle_host(const char* host);
    bool     connecttohost(const char* host, const char* user = "", const char* pwd = "");
    bool     connecttospeech(const char* speech, const char* lang);
    bool connecttoSD(String sdfile, int32_t fileStartPos);
    bool connecttoSD(const char* path, int32_t fileStartPos);
    bool connecttoFS(fs::FS &fs, const char* path, int32_t fileStartPos = -1);
//    bool     setFileLoop(bool input); 									//TEST loop
    void     setConnectionTimeout(uint16_t timeout_ms, uint16_t timeout_ms_ssl);
    bool     setAudioPlayPosition(uint16_t sec);
    bool     setFilePos(uint32_t pos);
//   bool audioFileSeek(const float speed);
    bool     setTimeOffset(int sec);
    bool     pauseResume();
    bool     isRunning() {return m_f_running;}
    void     loop();
    uint32_t  stopSong(); 			// Finish playing a song. Call this after the last playChunk call.
    void     startSong(); 			// Prepare to start playing. Call this each time a new song starts.
    void     forceMono(bool m);

    uint32_t getAudioDataStartPos();
    uint32_t getFileSize();
    uint32_t getFilePos();
//    uint32_t getSampleRate();
//    uint8_t  getBitsPerSample();
//    uint8_t  getChannels();
//    uint32_t getBitRate(bool avg = false);
    uint32_t getAudioFileDuration();
    uint32_t getAudioCurrentTime();
    uint32_t getTotalPlayingTime();

    uint32_t inBufferFilled(); // returns the number of stored bytes in the inputbuffer
//    uint32_t inBufferFree();   // returns the number of free bytes in the inputbuffer
//    uint32_t    getInBufferSize();           // returns the size of the inputbuffer in bytes
//    uint32_t inBufferSize();   // returns the size of the inputbuffer in bytes
    int getCodec() {return m_codec;}
    const char *getCodecname() {return codecname[m_codec];}
//    const char *getVersion() {return audioI2SVers;}

private:

//  enum : int8_t { AUDIOLOG_PATH_IS_NULL = -1, AUDIOLOG_FILE_NOT_FOUND = -2, AUDIOLOG_OUT_OF_MEMORY = -3, AUDIOLOG_FILE_READ_ERR = -4, AUDIOLOG_M4A_ATOM_NOT_FOUND = -5,  AUDIOLOG_ERR_UNKNOWN = -127 };

    const char *codecname[10] = {"unknown", "WAV", "MP3", "AAC", "M4A", "FLAC", "AACP", "OPUS", "OGG", "VORBIS" };
    enum : int { APLL_AUTO = -1, APLL_ENABLE = 1, APLL_DISABLE = 0 };
    enum : int { FORMAT_NONE = 0, FORMAT_M3U = 1, FORMAT_PLS = 2, FORMAT_ASX = 3, FORMAT_M3U8 = 4}; // playlist formats
    enum : int { AUDIO_NONE, HTTP_RESPONSE_HEADER , AUDIO_DATA, AUDIO_LOCALFILE, AUDIO_PLAYLISTINIT, AUDIO_PLAYLISTHEADER, AUDIO_PLAYLISTDATA};
    enum : int { FLAC_BEGIN = 0, FLAC_MAGIC = 1, FLAC_MBH =2, FLAC_SINFO = 3, FLAC_PADDING = 4, FLAC_APP = 5, FLAC_SEEK = 6, FLAC_VORBIS = 7, FLAC_CUESHEET = 8, FLAC_PICTURE = 9, FLAC_OKAY = 100};
    enum : int { M4A_BEGIN = 0, M4A_FTYP = 1, M4A_CHK = 2, M4A_MOOV = 3, M4A_FREE = 4, M4A_TRAK = 5, M4A_MDAT = 6, M4A_ILST = 7, M4A_MP4A = 8, M4A_AMRDY = 99, M4A_OKAY = 100};
    enum : int { CODEC_NONE = 0, CODEC_WAV = 1, CODEC_MP3 = 2, CODEC_AAC = 3, CODEC_M4A = 4, CODEC_FLAC = 5, CODEC_AACP = 6, CODEC_OPUS = 7, CODEC_OGG = 8, CODEC_VORBIS = 9};
    enum : int { ST_NONE = 0, ST_WEBFILE = 1, ST_WEBSTREAM = 2};

    typedef struct _pis_array{
        int number;
        int pids[4];
    } pid_array;

    File 			m_audiofile;    // @suppress("Abstract class cannot be instantiated")
    WiFiClient 		client;       // @suppress("Abstract class cannot be instantiated")
    WiFiClientSecure 	clientsecure; // @suppress("Abstract class cannot be instantiated")
    WiFiClient* 		_client = nullptr;

    SemaphoreHandle_t     mutex_playAudioData;
    SemaphoreHandle_t     mutex_audioTask;
    TaskHandle_t          m_audioTaskHandle = nullptr;


private:
    uint8_t       cs_pin; 		// Pin where CS line is connected
    uint8_t       dcs_pin; 		// Pin where DCS line is connected
    uint8_t       dreq_pin; 		// Pin where DREQ line is connected
//    uint8_t       mosi_pin; 		// Pin where MOSI line is connected
//    uint8_t       miso_pin; 		// Pin where MISO line is connected
//    uint8_t       sclk_pin; 		// Pin where SCLK line is connected
    uint8_t       curvol; 				// Current volume setting 0..100%
    int8_t         m_balance = 0; 		// -16 (mute left) ... +16 (mute right)

    const uint8_t vs1053_chunk_size = 32 ;
    // SCI Register
    const uint8_t SCI_MODE          = 0x0 ;
    const uint8_t SCI_STATUS        = 0x1 ;
    const uint8_t SCI_BASS          = 0x2 ;
    const uint8_t SCI_CLOCKF        = 0x3 ;
    const uint8_t SCI_DECODE_TIME   = 0x4 ;        // current decoded time in full seconds
    const uint8_t SCI_AUDATA        = 0x5 ;
    const uint8_t SCI_WRAM          = 0x6 ;
    const uint8_t SCI_WRAMADDR      = 0x7 ;
    const uint8_t SCI_HDAT0         = 0x8 ; 		// (Read average bitRate from WRAM register)
    const uint8_t SCI_HDAT1         = 0x9 ; 		// (Read laer and ID from WRAM register)
    const uint8_t SCI_AIADDR        = 0xA ;
    const uint8_t SCI_VOL           = 0xB ;
    const uint8_t SCI_AICTRL0       = 0xC ;
    const uint8_t SCI_AICTRL1       = 0xD ;
    const uint8_t SCI_AICTRL2       = 0xE ;
    const uint8_t SCI_AICTRL3       = 0xF ;
    // SCI_MODE bits
    const uint8_t SM_SDINEW         = 11 ;        	// Bitnumber in SCI_MODE always on
    const uint8_t SM_RESET          = 2 ;        	// Bitnumber in SCI_MODE soft reset
    const uint8_t SM_CANCEL         = 3 ;         	// Bitnumber in SCI_MODE cancel song
    const uint8_t SM_TESTS          = 5 ;         	// Bitnumber in SCI_MODE for tests
    const uint8_t SM_LINE1          = 14 ;        	// Bitnumber in SCI_MODE for Line input

    SPIClass*       spi_VS1053 = NULL;
    SPISettings     VS1053_SPI_DATA;                // SPI settings normal speed
    SPISettings     VS1053_SPI_CTL;                 // SPI settings control mode

#pragma GCC diagnostic pop

    std::vector<ps_ptr<char>> m_playlistContent;  // m3u8 playlist buffer
    std::vector<ps_ptr<char>> m_playlistURL;      // m3u8 streamURLs buffer
    std::vector<ps_ptr<char>> m_linesWithSeqNrAndURL;   // extract from m_playlistContent, contains URL and MediaSequenceNumber
    std::vector<ps_ptr<char>> m_linesWithEXTINF;        // extract from m_playlistContent, contains length and metadata
    std::vector<uint32_t>     m_hashQueue;

    const size_t    m_frameSizeWav       = 4096;
    const size_t    m_frameSizeMP3       = 1600 * 2;
    const size_t    m_frameSizeAAC       = 1600;
    const size_t    m_frameSizeFLAC      = 4096 * 6; // 24576
    const size_t    m_frameSizeOPUS      = 2048;
    const size_t    m_frameSizeVORBIS    = 4096 * 2;
    const size_t    m_outbuffSize          = 4096 * 2;
//    const size_t    m_samplesBuff48KSize = m_outbuffSize * 8; // 131072KB  SRmin: 6KHz -> SRmax: 48K

    static const uint8_t m_tsPacketSize  = 188;
    static const uint8_t m_tsHeaderSize  = 4;

    ps_ptr<int16_t> m_outBuff;        // Interleaved L/R
//    ps_ptr<int16_t> m_samplesBuff48K; // Interleaved L/R
    char*           m_chbuf = NULL;
    uint16_t       m_chbufSize = 512 + 64;                // will set in constructor (depending on PSRAM)
    ps_ptr<char>     m_ibuff;          // used in log_info()
    ps_ptr<char>     m_lastHost;       // Store the last URL to a webstream
    ps_ptr<char>     m_currentHost;    // can be changed by redirection or playlist
    ps_ptr<char>     m_lastM3U8host;
    ps_ptr<char>     m_speechtxt;      // stores tts text
    ps_ptr<char>     m_streamTitle;    // stores the last StreamTitle


    uint8_t  vuLeft, vuRight;
//    filter_t        m_filter[3];                    // digital filters
    const uint16_t  m_plsBuffEntryLen = 256;        // length of each entry in playlistBuff
    int              m_LFcount;                      // Detection of end of header
    uint32_t        m_bitRate=0;                    // current bitrate given fom decoder
    uint32_t        m_avr_bitrate = 0;              // average bitrate, median computed by VBR
    int             m_readbytes = 0;                // bytes read
    uint32_t        m_metacount = 0;                // counts down bytes between metadata
    int             m_controlCounter = 0;           // Status within readID3data() and readWaveHeader()
//    int16_t         m_inputHistory[6] = {0};        // used in resampleTo48kStereo()
//    uint16_t        m_opus_mode = 0;                // celt_only, silk_only or hybrid
//    uint8_t         m_channels = 2;

    char*           m_playlistBuff = NULL;          // stores playlistdata
    uint8_t         m_codec = CODEC_NONE;           //
    uint8_t         m_m3u8Codec = CODEC_AAC;        // codec of m3u8 stream
    uint8_t         m_expectedCodec = CODEC_NONE;   // set in connecttohost (e.g. http://url.mp3 -> CODEC_MP3)
    uint8_t         m_expectedPlsFmt = FORMAT_NONE; // set in connecttohost (e.g. streaming01.m3u) -> FORMAT_M3U)
    uint8_t         m_streamType = ST_NONE;
    uint8_t         m_rev=0;                        // Revision
    uint8_t         m_playlistFormat = 0;           // M3U, PLS, ASX
    size_t          m_ibuffSize = 0;                // log buffer size for audio_info() will set in constructor (depending on PSRAM)
    size_t          m_fileSize = 0;                // size of the file
    size_t          m_audioDataSize = 0;            //
    uint32_t       m_audioDataStart = 0;           // in bytes
    float            m_audioCurrentTime = 0;
    uint32_t       m_audioFileDuration = 0;
    bool            m_localBitrateSend = true;
    uint8_t        m_ID3Size = 0;                  // lengt of ID3frame - ID3header
    bool            m_f_ssl=false;
    uint8_t         m_endFillByte;                 // Byte to send when stopping song
    uint8_t         m_M4A_objectType = 0;           // set in read_M4A_Header
    uint8_t         m_M4A_chConfig = 0;             // set in read_M4A_Header
//    uint16_t        m_M4A_sampleRate = 0;           // set in read_M4A_Header
    uint16_t       m_dataMode{0};                  // Statemaschine
    bool            m_f_chunked = false;           // Station provides chunked transfer
//    bool            m_f_unknownFileLength = false;  // file length unknown
//    bool            m_f_clientIsConnected = false;  // client connected, inter task communication
    bool            m_f_ctseen=false;               // First line of header seen or not
    bool            m_f_firstchunk=true;            // First chunk as input
    bool            m_f_metadata = false;           // assume stream without metadata
    bool            m_f_firstmetabyte = false;      // True if first metabyte (counter)
    bool            m_f_playing = false;            // valid mp3 stream recognized
    bool            m_f_tts = false;                // text to speech
    bool            m_f_ogg = false;                // OGG stream
    bool            m_f_Log = false;                // set in platformio.ini  -DAUDIO_LOG and -DCORE_DEBUG_LEVEL=3 or 4
//    bool            m_f_loop = false;               // Set if audio file should loop
    bool            m_f_forceMono = false;          // if true stereo -> mono
    bool            m_f_rtsp = false;               // set if RTSP is used (m3u8 stream)
    bool            m_f_continue = false;           // next m3u8 chunk is available
    bool            m_f_ts = true;                  // transport stream
    bool            m_f_webfile = false;
    bool            m_f_firstCall = false;          // InitSequence for processWebstream and processLokalFile
    bool            m_f_firstM3U8call = false;      // InitSequence for m3u8 parsing
    bool            m_f_firstCurTimeCall = false;   // InitSequence for computeAudioTime
    bool            m_f_firstPlayCall = false;      // InitSequence for playAudioData
    bool            m_f_m3u8data = false;           // used in processM3U8entries
    bool            m_f_psramFound = false;         // set in constructor, result of psramInit()
    bool            m_f_timeout = false;            //
    bool            m_f_allDataReceived = false;
    uint32_t        m_chunkcount = 0 ;              // Counter for chunked transfer
    uint32_t        m_contentlength = 0;
    uint32_t        m_bytesNotDecoded = 0;          // pictures or something else that comes with the stream
    uint32_t        m_PlayingStartTime = 0;         // Stores the milliseconds after the start of the audio
    int32_t         m_resumeFilePos = -1;           // the return value from stopSong(), (-1) is idle
    int32_t         m_fileStartPos = -1;            // may be set in connecttoFS()
    uint32_t        m_haveNewFilePos = 0;           // user changed the file position
    uint32_t        m_sumBytesDecoded = 0;          // used for streaming
    uint32_t        m_webFilePos = 0;               // same as audiofile.position() for SD files
    uint8_t         m_flacBitsPerSample = 0;        // bps should be 16
    uint8_t         m_flacNumChannels = 0;          // can be read out in the FLAC file header
    uint32_t        m_flacSampleRate = 0;           // can be read out in the FLAC file header
    uint16_t        m_flacMaxFrameSize = 0;         // can be read out in the FLAC file header
    uint16_t        m_flacMaxBlockSize = 0;         // can be read out in the FLAC file header
    uint32_t        m_stsz_numEntries = 0;          // num of entries inside stsz atom (uint32_t)
    uint32_t        m_stsz_position = 0;            // pos of stsz atom within file
    uint32_t        m_flacTotalSamplesInStream = 0; // can be read out in the FLAC file header
    uint32_t        m_metaint = 0;                  // Number of databytes between metadata
    uint32_t        m_t0 = 0;                       // store millis(), is needed for a small delay
    uint16_t        m_streamTitleHash = 0;          // remember streamtitle, ignore multiple occurence in metadata
    int16_t         m_btp=0;                        // Bytes to play
    int16_t         m_validSamples = {0};           // #144
    uint16_t        m_streamUrlHash = 0;            // remember streamURL, ignore multiple occurence in metadata
    uint16_t        m_timeout_ms = 250;
    uint16_t        m_timeout_ms_ssl = 2700;
    uint16_t        m_m3u8_targetDuration = 10;     //
    bool            m_f_running = false;
//	    bool            m_f_localfile = false;         // Play from local mp3-file		?
    bool            m_f_ID3v1TagFound = false;      // ID3v1 tag found
    bool            m_f_webstream = false;         // Play from URL
    bool            m_f_stream = false;             // stream ready for output?
    bool            m_f_decode_ready = false;       // if true data for decode are ready
    bool            m_f_eof = false;                // end of file
    bool            m_f_lockInBuffer = false;       // lock inBuffer for manipulation
    bool            m_f_audioTaskIsDecoding = false;
    bool            m_f_acceptRanges = false;
    bool            m_f_reset_m3u8Codec = true;     // reset codec for m3u8 stream
    bool            m_f_connectionClose = false;    // set in parseHttpResponseHeader
    bool            m_f_unsync = false;
    bool            m_f_exthdr = false;             // ID3 extended header
    bool            m_f_m4aID3dataAreRead = false;  // has the m4a-ID3data already been read?
    bool            _vuInitalized = false;            // true if VUmeter is enabled

//***************************************************************************************
//int32_t  OGG_specialIndexOf(uint8_t* base, const char* str, int32_t baselen, bool exact = false);
int32_t   VORBISFindSyncWord(unsigned char *buf, int32_t nBytes);
int32_t  FLACFindSyncWord(unsigned char *buf, int32_t nBytes);
int32_t  OPUSFindSyncWord(unsigned char *buf, int32_t nBytes);
int8_t    parseVorbisComment(uint8_t *inbuf, int16_t nBytes);
int8_t    parseFlacComment(uint8_t *inbuf, int16_t nBytes);
int8_t    parseOpusComment(uint8_t *inbuf, int32_t nBytes);
char*     VORBISgetStreamTitle();
char*     FLACgetStreamTitle();
char*     OPUSgetStreamTitle();
bool      s_f_vorbisNewSteamTitle = false;  			// VORBIS streamTitle
bool      s_f_flacNewStreamtitle = false;  			// FLAC streamTitle
bool      s_f_opusNewSteamTitle = false;  			// OPUS streamTitle
char*      s_vorbisChbuf = NULL;
char*      s_flacStreamTitle = NULL;
char*      s_opusSteamTitle = NULL;
//uint32_t FLACgetMetadataBlockPicture();
//uint32_t OPUSgetMetadataBlockPicture();
//uint32_t VORBISgetMetadataBlockPicture();
//    bool      read_M4A_Lirics(uint8_t *data, size_t len);
//    bool      read_FLAC_VORBIS_Lirics(uint8_t *data, size_t len);

//    uint16_t getDecodedTime();		 // Provides SCI_DECODE_TIME register value
//    void clearDecodedTime();		// Clears SCI_DECODE_TIME register (sets 0x00)
//    void enableI2sOut(VS1053_I2S_RATE i2sRate = VS1053_I2S_RATE_48_KHZ);	// enable I2S output (GPIO4=LRCLK/WSEL; GPIO5=MCLK; GPIO6=SCLK/BCLK; GPIO7=SDATA/DOUT)
//    void disableI2sOut();			// disable I2S output; this is the default state
//*****************************************************************************************


protected:

    #ifndef ESP_ARDUINO_VERSION_VAL
        #define ESP_ARDUINO_VERSION_MAJOR 0
        #define ESP_ARDUINO_VERSION_MINOR 0
        #define ESP_ARDUINO_VERSION_PATCH 0
    #endif

/*    #if ESP_IDF_VERSION_MAJOR < 5
        inline void DCS_HIGH() {(dcs_pin&0x20) ? GPIO.out1_w1ts.data = 1 << (dcs_pin - 32) : GPIO.out_w1ts = 1 << dcs_pin;}
        inline void DCS_LOW()  {(dcs_pin&0x20) ? GPIO.out1_w1tc.data = 1 << (dcs_pin - 32) : GPIO.out_w1tc = 1 << dcs_pin;}
        inline void CS_HIGH()  {( cs_pin&0x20) ? GPIO.out1_w1ts.data = 1 << ( cs_pin - 32) : GPIO.out_w1ts = 1 <<  cs_pin;}
        inline void CS_LOW()   {( cs_pin&0x20) ? GPIO.out1_w1tc.data = 1 << ( cs_pin - 32) : GPIO.out_w1tc = 1 <<  cs_pin;}
    #else	*/
        inline void DCS_HIGH() {gpio_set_level((gpio_num_t)dcs_pin, 1);}
        inline void DCS_LOW()  {gpio_set_level((gpio_num_t)dcs_pin, 0);}
        inline void CS_HIGH()  {gpio_set_level((gpio_num_t) cs_pin, 1);}
        inline void CS_LOW()   {gpio_set_level((gpio_num_t) cs_pin, 0);}
//    #endif

    inline void await_data_request() {while(!digitalRead(dreq_pin)) NOP();}	  // Very short delay
    inline bool data_request()     {return(digitalRead(dreq_pin) == HIGH);}

    void     initInBuff();
    void     control_mode_on();
    void     control_mode_off();
    void     data_mode_on();
    void     data_mode_off();
    uint16_t read_register ( uint8_t _reg ) ;
    void     write_register ( uint8_t _reg, uint16_t _value );
    void     sdi_send_buffer ( uint8_t* data, size_t len );
    void     sdi_send_fillers ( size_t length );
    void     wram_write ( uint16_t address, uint16_t data );
    uint16_t wram_read ( uint16_t address );
    void     loadUserCode();

private:
    // ------- PRIVATE MEMBERS ----------------------------------------

    void     latinToUTF8(ps_ptr<char>& buff, bool UTF8check = true);
    void     htmlToUTF8(char* str);
    bool     httpPrint(const char* host);
    bool     httpRange(const char* host, uint32_t range);
    void     processLocalFile();
    void     processWebStream();
    void     processWebFile();
    void     processWebStreamTS();
    void     processWebStreamHLS();
    void     playAudioData();
    bool     readPlayListData();
    const char* parsePlaylist_M3U();
    const char* parsePlaylist_PLS();
    const char* parsePlaylist_ASX();
    ps_ptr<char>    parsePlaylist_M3U8();
    ps_ptr<char>    m3u8redirection(uint8_t* codec);
    bool            m3u8_findMediaSeqInURL(std::vector<ps_ptr<char>>&linesWithSeqNrAndURL, uint64_t* mediaSeqNr);
//	    void showCodecParams();
    int       findNextSync(uint8_t* data, size_t len);
    size_t   sendBytes(uint8_t* data, size_t len);
//    void            computeAudioTime(uint16_t bytesDecoderIn, uint16_t bytesDecoderOut);
    void            showID3Tag(const char* tag, const char* val);
    size_t          readAudioHeader(uint32_t bytes);
    int             read_WAV_Header(uint8_t* data, size_t len);
    int             read_FLAC_Header(uint8_t* data, size_t len);
//    int          read_OPUS_Header(uint8_t* data, size_t len);
//    int          read_VORBIS_Header(uint8_t* data, size_t len);
//	  int  read_OGG_Header(uint8_t *data, size_t len);
    int             read_ID3_Header(uint8_t* data, size_t len);
    int             read_M4A_Header(uint8_t* data, size_t len);
    size_t          process_m3u8_ID3_Header(uint8_t* packet);
//    void            playChunk();
    void            showstreamtitle(char* ml);
    bool            parseContentType(char* ct);
    bool            parseHttpResponseHeader();
//    void        printProcessLog(int r, const char* s = "");
    inline uint32_t streamavail() { return _client ? _client->available() : 0; }
    bool            ts_parsePacket(uint8_t* packet, uint8_t* packetStart, uint8_t* packetLength);
    uint32_t        find_m4a_atom(uint32_t fileSize, const char* atomType, uint32_t depth = 0);

    //+++ create a T A S K  for playAudioData(), output via I2S +++
  public:
    void            setAudioTaskCore(uint8_t coreID);
    uint32_t        getHighWatermark();
  private:
    void            startAudioTask(); // starts a task for decode and play
    void            stopAudioTask();  // stops task for audio
    static void     taskWrapper(void *param);
    void            audioTask();
    void            performAudioTask();
    #ifdef AUDIOTASK_CORE
      uint8_t       m_audioTaskCoreId = AUDIOTASK_CORE;  // Set from platformio.ini
    #else
      uint8_t       m_audioTaskCoreId = 1;  // Default core 1
    #endif
    bool           m_f_audioTaskIsRunning = false;

    //+++ W E B S T R E A M  -  H E L P   F U N C T I O N S +++
    uint16_t readMetadata(uint16_t b, bool first = false);
    size_t   readChunkSize(uint8_t* bytes);
    bool     readID3V1Tag();
    boolean  streamDetection(uint32_t bytesAvail);
    void     seek_m4a_stsz();
    void     seek_m4a_ilst();
    uint32_t m4a_correctResumeFilePos();
    uint32_t ogg_correctResumeFilePos();
    int32_t  flac_correctResumeFilePos();
    int32_t  mp3_correctResumeFilePos();
    uint8_t  determineOggCodec(uint8_t* data, uint16_t len);

    //++++ implement several function with respect to the index of string ++++
    void strlower(char* str) {
        unsigned char* p = (unsigned char*)str;
        while(*p) {
            *p = tolower((unsigned char)*p);
            p++;
        }
    }

    void trim(char *str) {
        char *start = str;  // keep the original pointer
        char *end;
        while (isspace((unsigned char)*start)) start++; // find the first non-space character

        if (*start == 0) {  // all characters were spaces
            str[0] = '\0';  // return a empty string
            return;
        }

        end = start + strlen(start) - 1;  // find the end of the string

        while (end > start && isspace((unsigned char)*end)) end--;
        end[1] = '\0';  // Null-terminate the string after the last non-space character

        // Move the trimmed string to the beginning of the memory area
        memmove(str, start, strlen(start) + 1);  // +1 for '\0'
    }

    bool startsWith (const char* base, const char* str) {
    //fb
        char c;
        while ( (c = *str++) != '\0' )
          if (c != *base++) return false;
        return true;
    }

    bool endsWith(const char *base, const char *searchString) {
        int32_t slen = strlen(searchString);
        if(slen == 0) return false;
        const char *p = base + strlen(base);
    //  while(p > base && isspace(*p)) p--;  // rtrim
        p -= slen;
        if(p < base) return false;
        return (strncmp(p, searchString, slen) == 0);
    }

    int indexOf (const char* base, const char* str, int startIndex = 0) {
    //fb
        const char *p = base;
        for (; startIndex > 0; startIndex--)
            if (*p++ == '\0') return -1;
        char* pos = strstr(p, str);
        if (pos == nullptr) return -1;
        return pos - base;
    }

    int indexOf (const char* base, char ch, int startIndex = 0) {
    //fb
        const char *p = base;
        for (; startIndex > 0; startIndex--)
            if (*p++ == '\0') return -1;
        char *pos = strchr(p, ch);
        if (pos == nullptr) return -1;
        return pos - base;
    }

    int lastIndexOf(const char* haystack, const char* needle) {
    //fb
        int nlen = strlen(needle);
        if (nlen == 0) return -1;
        const char *p = haystack - nlen + strlen(haystack);
        while (p >= haystack) {
          int i = 0;
          while (needle[i] == p[i])
            if (++i == nlen) return p - haystack;
          p--;
        }
        return -1;
    }

    int lastIndexOf(const char* haystack, const char needle) {
    //fb
        const char *p = strrchr(haystack, needle);
        return (p ? p - haystack : -1);
    }

    int specialIndexOf (uint8_t* base, const char* str, int baselen, bool exact = false){
        int result = 0;  // seek for str in buffer or in header up to baselen, not nullterninated
        if (strlen(str) > baselen) return -1; // if exact == true seekstr in buffer must have "\0" at the end
        for (int i = 0; i < baselen - strlen(str); i++){
            result = i;
            for (int j = 0; j < strlen(str) + exact; j++){
                if (*(base + i + j) != *(str + j)){
                    result = -1;
                    break;
                }
            }
            if (result >= 0) break;
        }
        return result;
    }

    int32_t min3(int32_t a, int32_t b, int32_t c){
        uint32_t min_val = a;
        if (b < min_val) min_val = b;
        if (c < min_val) min_val = c;
        return min_val;
    }

    // some other functions
    uint64_t bigEndian(uint8_t* base, uint8_t numBytes, uint8_t shiftLeft = 8) {
        uint64_t result = 0;  // Use uint64_t for greater caching
        if(numBytes < 1 || numBytes > 8) return 0;
        for (int i = 0; i < numBytes; i++) {
            result |= (uint64_t)(*(base + i)) << ((numBytes - i - 1) * shiftLeft); //Make sure the calculation is done correctly
        }
        if(result > SIZE_MAX) {
            log_e("range overflow");
            return 0;
        }
        return result;
    }

    bool b64encode(const char* source, uint16_t sourceLength, char* dest){
        size_t size = base64_encode_expected_len(sourceLength) + 1;
        char * buffer = (char *) malloc(size);
        if(buffer) {
            base64_encodestate _state;
            base64_init_encodestate(&_state);
            int len = base64_encode_block(&source[0], sourceLength, &buffer[0], &_state);
            len = base64_encode_blockend((buffer + len), &_state);
            memcpy(dest, buffer, strlen(buffer));
            dest[strlen(buffer)] = '\0';
            free(buffer);
            return true;
        }
        return false;
    }

    void vector_clear_and_shrink(std::vector<ps_ptr<char>>& vec){
        for(int i = 0; i< vec.size(); i++) vec[i].reset();
        vec.clear();            // unique_ptr takes care of free()
        vec.shrink_to_fit();    // put back memory
    }

    uint32_t simpleHash(const char* str){
        if(str == NULL) return 0;
        uint32_t hash = 0;
        for(int i=0; i<strlen(str); i++){
		    if(str[i] < 32) continue; // ignore control sign
		    hash += (str[i] - 31) * i * 32;
        }
        return hash;
	}

    ps_ptr<char> urlencode(const char* str, bool spacesOnly) {
        if (!str) {return {};}  // Enter is zero

        // Reserve memory for the result (3x the length of the input string, worst-case)
        size_t inputLength = strlen(str);
        size_t bufferSize = inputLength * 3 + 1; // Worst-case-Szenario
        ps_ptr<char>encoded;
        encoded.alloc(bufferSize);
        if (!encoded.valid()) {return {}; } // memory allocation failed

        const char *p_input = str;  // Copy of the input pointer
        char *p_encoded = encoded.get();  // pointer of the output buffer
        size_t remainingSpace = bufferSize; // remaining space in the output buffer

        while (*p_input) {
            if (isalnum((unsigned char)*p_input)) {
                // adopt alphanumeric characters directly
                if (remainingSpace > 1) {
                    *p_encoded++ = *p_input;
                    remainingSpace--;
                } else {
                    return {}; // security check failed
                }
            } else if (spacesOnly && *p_input != 0x20) {
                // Nur Leerzeichen nicht kodieren
                if (remainingSpace > 1) {
                    *p_encoded++ = *p_input;
                    remainingSpace--;
                } else {
                    return {}; // security check failed
                }
            } else {
                // encode unsafe characters as '%XX'
                if (remainingSpace > 3) {
                    int written = snprintf(p_encoded, remainingSpace, "%%%02X", (unsigned char)*p_input);
                    if (written < 0 || written >= (int)remainingSpace) {
                        return {}; // error writing to buffer
                    }
                    p_encoded += written;
                    remainingSpace -= written;
                } else {
                    return {}; // security check failed
                }
            }
            p_input++;
        }

        // Null-terminieren
        if (remainingSpace > 0) {
            *p_encoded = '\0';
        } else {
            return {}; // security check failed
        }
        encoded.shrink_to_fit();
        return encoded;
    }

// Function to reverse the byte order of a 32-bit value (big-endian to little-endian)
    uint32_t bswap32(uint32_t x) {
        return ((x & 0xFF000000) >> 24) |
               ((x & 0x00FF0000) >> 8)  |
               ((x & 0x0000FF00) << 8)  |
               ((x & 0x000000FF) << 24);
    }

// Function to reverse the byte order of a 64-bit value (big-endian to little-endian)
    uint64_t bswap64(uint64_t x) {
        return ((x & 0xFF00000000000000ULL) >> 56) |
               ((x & 0x00FF000000000000ULL) >> 40) |
               ((x & 0x0000FF0000000000ULL) >> 24) |
               ((x & 0x000000FF00000000ULL) >> 8)  |
               ((x & 0x00000000FF000000ULL) << 8)  |
               ((x & 0x0000000000FF0000ULL) << 24) |
               ((x & 0x000000000000FF00ULL) << 40) |
               ((x & 0x00000000000000FFULL) << 56);
    }

};

#endif	// #ifndef _vs1053_ext
#endif	// #if VS1053_CS!=255
