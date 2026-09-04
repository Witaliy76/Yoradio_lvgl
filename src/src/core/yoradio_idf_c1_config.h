#ifndef YORADIO_IDF_C1_CONFIG_H
#define YORADIO_IDF_C1_CONFIG_H

// Compile-time view paired atomically with the accepted production ESP-IDF archives (C1 + FU3 liblwip.a).
// yoradio_build.py force-includes this header only after the full 6/6 overlay
// validates, so a stock-library fallback keeps the stock sdkconfig unchanged.
#include "sdkconfig.h"

#if defined(YORADIO_IDF_C1_CONFIG) && YORADIO_IDF_C1_CONFIG
  #undef CONFIG_LWIP_MAX_ACTIVE_TCP
  #define CONFIG_LWIP_MAX_ACTIVE_TCP 64

  #undef CONFIG_LWIP_TCP_WND_DEFAULT
  #define CONFIG_LWIP_TCP_WND_DEFAULT 32768

  #undef CONFIG_LWIP_TCPIP_CORE_LOCKING_INPUT
  #define CONFIG_LWIP_TCPIP_CORE_LOCKING_INPUT 1

  // FU3: balanced streaming profile. The stock framework sdkconfig.h still
  // reports 6 / 5744; the overlay liblwip.a is built with 32 / 8192, so the
  // compile-time view must follow the archive or the two disagree.
  #undef CONFIG_LWIP_TCP_RECVMBOX_SIZE
  #define CONFIG_LWIP_TCP_RECVMBOX_SIZE 32

  #undef CONFIG_LWIP_TCP_SND_BUF_DEFAULT
  #define CONFIG_LWIP_TCP_SND_BUF_DEFAULT 8192
#endif

#endif
