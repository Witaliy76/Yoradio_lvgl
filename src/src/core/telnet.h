#ifndef telnet_h
#define telnet_h

#include <WiFi.h>

#define MAX_TLN_CLIENTS 5
#define MAX_PRINTF_LEN BUFLEN+50

class Telnet {
  public:
    Telnet() {};
    bool begin(bool quiet=false);
    void loop();
    void stop();
    void print(uint8_t id, const char *buf);
    void print(const char *buf);
    void printf(uint8_t id, const char *format, ...);
    void printf(const char *format, ...);
    void cleanupClients();
    void info();
    // Block 8-E1: full diag body (bypasses printf MAX_PRINTF_LEN cap).
    void printDiagBody(uint8_t clientId, const char* body);
  protected:
    WiFiServer server = WiFiServer(23);
    WiFiClient clients[MAX_TLN_CLIENTS];
    void emptyClientStream(WiFiClient client);
    void on_connect(const char* str, uint8_t clientId);
    void on_input(const char* str, uint8_t clientId);
  private:
    bool _isIPSet(IPAddress ip);
    void handleSerial();
};

extern Telnet telnet;

#endif
