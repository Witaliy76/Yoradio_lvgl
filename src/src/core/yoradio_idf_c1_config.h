#ifndef YORADIO_IDF_C1_CONFIG_H
#define YORADIO_IDF_C1_CONFIG_H

// Compile-time view paired atomically with the accepted C1 ESP-IDF archives.
// yoradio_build.py force-includes this header only after the full 7/7 overlay
// validates, so a stock-library fallback keeps the stock sdkconfig unchanged.
#include "sdkconfig.h"

#if defined(YORADIO_IDF_C1_CONFIG) && YORADIO_IDF_C1_CONFIG
  #undef CONFIG_LWIP_MAX_ACTIVE_TCP
  #define CONFIG_LWIP_MAX_ACTIVE_TCP 64

  #undef CONFIG_LWIP_TCP_WND_DEFAULT
  #define CONFIG_LWIP_TCP_WND_DEFAULT 32768

  #undef CONFIG_LWIP_TCPIP_CORE_LOCKING_INPUT
  #define CONFIG_LWIP_TCPIP_CORE_LOCKING_INPUT 1
#endif

#endif
