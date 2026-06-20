#include "net_dns_resolver.h"

#include <Arduino.h>
#include <WiFiUdp.h>          // typedef NetworkUDP / WiFiUDP
#include <esp_random.h>       // esp_random() for TX ID seed
#include <esp_heap_caps.h>    // heap diagnostics under YORADIO_NET_DNS_DIAG
#include <freertos/semphr.h>  // SemaphoreHandle_t, xSemaphoreCreateMutex …
#include <string.h>

/*
 * net_dns_resolver — bounded synchronous DNS A-query to a caller-selected server.
 * Resolves a hostname by sending one UDP/53 packet to the chosen resolver IP and
 * parsing the response in a file-scope static buffer.
 *
 * Memory model:
 *   s_dns_pkt — 512 B static (.bss) — guarded by s_dns_mutex; NOT on the caller stack.
 *   s_dns_mutex — lazy creation, bounded wait — serialises the shared buffer.
 *
 * Responsibilities: hostname encoding, UDP send/recv, DNS parsing, deduplication.
 * NOT responsible for: Weather, HTTP, scheduler, API keys, persistent IP cache.
 */

// ── Compile-time diagnostics gate (default OFF / выключено по умолчанию) ────
#ifndef YORADIO_NET_DNS_DIAG
#define YORADIO_NET_DNS_DIAG 0
#endif

// ── Editable constants block — DNS fallback servers and protocol tunables ────
// Edit only these lines to change the compiled fallback resolver list.
// Редактируйте только этот блок для изменения скомпилированных fallback-серверов.
namespace {

const NetDnsServer kDefaultFallbackDnsServers[] = {
    { "cloudflare", IPAddress(1, 1, 1, 1) },
    { "quad9",      IPAddress(9, 9, 9, 9) },
};
constexpr size_t kDefaultFallbackDnsServerCount =
    sizeof(kDefaultFallbackDnsServers) / sizeof(kDefaultFallbackDnsServers[0]);

// DNS/UDP protocol constants — do not change without protocol justification.
constexpr uint16_t kDnsPort            = 53;
constexpr size_t   kDnsPacketCapacity  = 512;   // classic UDP DNS limit (no EDNS0 advertised)
constexpr uint32_t kMutexWaitMs        = 1500;  // bounded mutex wait — never portMAX_DELAY here
constexpr uint8_t  kMaxPointerJumps    = 8;     // pointer-chain cap — defeats loops / циклы
constexpr uint8_t  kMaxParsedRR        = 32;    // max answer records to iterate

// DNS header bit/mask helpers (network-byte-order fields assumed already swapped)
constexpr uint16_t kDnsFlagQR     = 0x8000u;
constexpr uint16_t kDnsFlagOpcode = 0x7800u;
constexpr uint16_t kDnsFlagTC     = 0x0200u;
constexpr uint16_t kDnsRcodeMask  = 0x000Fu;
constexpr uint16_t kDnsTypeA      = 1u;
constexpr uint16_t kDnsClassIN    = 1u;

// ── Static packet buffer — guarded by s_dns_mutex ───────────────────────────
// MUST remain in .bss; never placed on any task stack.
// ОБЯЗАН оставаться в .bss; никогда не на стеке задачи.
uint8_t s_dns_pkt[kDnsPacketCapacity];

// ── Helper-owned mutex — lazy creation matching project pattern ──────────────
SemaphoreHandle_t s_dns_mutex = nullptr;

SemaphoreHandle_t dnsMutex() {
    if (!s_dns_mutex) {
        s_dns_mutex = xSemaphoreCreateMutex();
    }
    return s_dns_mutex;
}

// ── Internal diagnostics helpers ─────────────────────────────────────────────
#if YORADIO_NET_DNS_DIAG
void dns_log_heap(const char* tag, const char* diagTag) {
    Serial.printf("[NET_DNS] heap[%s] tag=%s int_free=%u int_block=%u\n",
                  tag, diagTag ? diagTag : "-",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
#endif

// ── Hostname → DNS label encoding ────────────────────────────────────────────
// Encodes "foo.bar.com" → \x03foo\x03bar\x03com\x00 into buf[offset…].
// Returns new offset on success, 0 on any encoding/overflow error.
// — validates label lengths; rejects empty inner labels; prevents OOB writes.
size_t encodeHostname(const char* hostname, uint8_t* buf, size_t capacity, size_t offset) {
    if (!hostname || hostname[0] == '\0') return 0;

    const size_t hlen = strlen(hostname);
    if (hlen > 253) return 0; // RFC max total hostname length

    const char* p = hostname;
    while (*p) {
        const char* dot = strchr(p, '.');
        size_t labelLen = dot ? (size_t)(dot - p) : strlen(p);

        if (labelLen == 0 && dot != nullptr) return 0; // empty interior label e.g. "foo..com"
        if (labelLen > 63)  return 0; // RFC label max
        if (offset + 1 + labelLen + 1 > capacity) return 0; // overflow guard

        buf[offset++] = (uint8_t)labelLen;
        memcpy(&buf[offset], p, labelLen);
        offset += labelLen;

        if (!dot) break;
        p = dot + 1;
    }
    // trailing empty-label root terminator
    if (offset >= capacity) return 0;
    buf[offset++] = 0x00;
    return offset;
}

// ── DNS name skipper — supports compressed names and plain labels ─────────────
// Returns offset just past the name, or 0 on error.
// jumpsLeft prevents pointer loops.
// — никогда не читает за пределами pktLen; завершается при malformed/loop.
size_t skipDnsName(const uint8_t* pkt, size_t pktLen, size_t offset, uint8_t jumpsLeft = kMaxPointerJumps) {
    while (offset < pktLen) {
        uint8_t len = pkt[offset];
        if (len == 0) {
            return offset + 1; // end of name
        }
        if ((len & 0xC0) == 0xC0) {
            // compressed pointer — consume 2 bytes; caller continues after this
            if (offset + 2 > pktLen) return 0;
            return offset + 2; // caller must NOT follow the pointer for skip purposes
        }
        if ((len & 0xC0) != 0) return 0; // reserved
        offset += 1 + len;
    }
    return 0; // ran off end
}

// ── A-record deduplication helper ────────────────────────────────────────────
bool isAlreadyInList(const IPAddress* list, size_t count, const IPAddress& ip) {
    for (size_t i = 0; i < count; ++i) {
        if (list[i] == ip) return true;
    }
    return false;
}

} // namespace

// ── Public accessor ───────────────────────────────────────────────────────────
const NetDnsServer* netDnsDefaultFallbackServers(size_t& count) {
    count = kDefaultFallbackDnsServerCount;
    return kDefaultFallbackDnsServers;
}

// ── Main query implementation ─────────────────────────────────────────────────
NetDnsQueryStatus netDnsQueryA(
    const char*      hostname,
    const IPAddress& dnsServer,
    IPAddress*       outAddresses,
    size_t           outCapacity,
    size_t&          outCount,
    uint32_t         timeoutMs,
    const char*      diagTag)
{
    outCount = 0;

    // ── Argument validation ───────────────────────────────────────────────────
    if (!hostname || hostname[0] == '\0' || !outAddresses || outCapacity == 0) {
        return NetDnsQueryStatus::InvalidArgument;
    }

    // ── Acquire the shared buffer mutex ──────────────────────────────────────
    SemaphoreHandle_t mutex = dnsMutex();
    if (!mutex) {
        return NetDnsQueryStatus::LockUnavailable;
    }
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(kMutexWaitMs)) != pdTRUE) {
        return NetDnsQueryStatus::LockUnavailable;
    }

    // From here, s_dns_pkt is exclusively ours.
    // Отсюда s_dns_pkt принадлежит исключительно нам.

#if YORADIO_NET_DNS_DIAG
    dns_log_heap("before", diagTag);
    Serial.printf("[NET_DNS] tag=%s resolver=%s hostname=%s timeout=%u\n",
                  diagTag ? diagTag : "-",
                  dnsServer.toString().c_str(),
                  hostname, (unsigned)timeoutMs);
#endif

    NetDnsQueryStatus result = NetDnsQueryStatus::Timeout; // default if we fall through

    // ── Build DNS query packet ────────────────────────────────────────────────
    const uint16_t txId = (uint16_t)(esp_random() & 0xFFFF);
    memset(s_dns_pkt, 0, 12); // clear header

    // Header: ID, Flags (QR=0 query, RD=1), QDCOUNT=1
    s_dns_pkt[0] = (uint8_t)(txId >> 8);
    s_dns_pkt[1] = (uint8_t)(txId & 0xFF);
    s_dns_pkt[2] = 0x01; // RD=1
    s_dns_pkt[3] = 0x00;
    s_dns_pkt[4] = 0x00; s_dns_pkt[5] = 0x01; // QDCOUNT=1
    s_dns_pkt[6] = 0x00; s_dns_pkt[7] = 0x00; // ANCOUNT=0
    s_dns_pkt[8] = 0x00; s_dns_pkt[9] = 0x00; // NSCOUNT=0
    s_dns_pkt[10]= 0x00; s_dns_pkt[11]= 0x00; // ARCOUNT=0

    size_t offset = 12;
    offset = encodeHostname(hostname, s_dns_pkt, kDnsPacketCapacity, offset);
    if (offset == 0 || offset + 4 > kDnsPacketCapacity) {
        xSemaphoreGive(mutex);
        return NetDnsQueryStatus::InvalidArgument;
    }

    // QTYPE=A(1), QCLASS=IN(1) — network byte order
    s_dns_pkt[offset++] = 0x00; s_dns_pkt[offset++] = 0x01; // QTYPE A
    s_dns_pkt[offset++] = 0x00; s_dns_pkt[offset++] = 0x01; // QCLASS IN
    const size_t queryLen = offset;

    // ── Send UDP packet ───────────────────────────────────────────────────────
    WiFiUDP udp;
    if (udp.begin(0) == 0) { // bind to any ephemeral port
        xSemaphoreGive(mutex);
        return NetDnsQueryStatus::UdpStartFailed;
    }

    if (udp.beginPacket(dnsServer, kDnsPort) == 0) {
        udp.stop();
        xSemaphoreGive(mutex);
        return NetDnsQueryStatus::UdpStartFailed;
    }
    udp.write(s_dns_pkt, queryLen);
    if (udp.endPacket() == 0) {
        udp.stop();
        xSemaphoreGive(mutex);
        return NetDnsQueryStatus::SendFailed;
    }

    // ── Wait for response ─────────────────────────────────────────────────────
    const uint32_t t0 = millis();
    int recvLen = 0;
    while ((uint32_t)(millis() - t0) < timeoutMs) {
        int avail = udp.parsePacket();
        if (avail > 0) {
            // Validate source before consuming
            if (udp.remoteIP() != dnsServer || udp.remotePort() != kDnsPort) {
                // Foreign packet — drain it and keep waiting
                // Чужой пакет — дренируем и ждём дальше
                while (udp.available()) udp.read();
                continue;
            }
            if (avail > (int)kDnsPacketCapacity) {
                // Oversized — drain and continue
                while (udp.available()) udp.read();
                continue;
            }
            recvLen = udp.read(s_dns_pkt, kDnsPacketCapacity);
            break;
        }
        delay(5);
    }

    udp.stop();

    const uint32_t elapsed = (uint32_t)(millis() - t0);

    if (recvLen < 12) {
        // Timed out or truncated beyond repair
#if YORADIO_NET_DNS_DIAG
        Serial.printf("[NET_DNS] tag=%s resolver=%s result=timeout elapsed_ms=%lu\n",
                      diagTag ? diagTag : "-", dnsServer.toString().c_str(), (unsigned long)elapsed);
#endif
        xSemaphoreGive(mutex);
        return NetDnsQueryStatus::Timeout;
    }

    const size_t pktLen = (size_t)recvLen;

    // ── Parse DNS response header ─────────────────────────────────────────────
    const uint16_t rxId    = ((uint16_t)s_dns_pkt[0] << 8) | s_dns_pkt[1];
    const uint16_t flags   = ((uint16_t)s_dns_pkt[2] << 8) | s_dns_pkt[3];
    const uint16_t qdcount = ((uint16_t)s_dns_pkt[4] << 8) | s_dns_pkt[5];
    const uint16_t ancount = ((uint16_t)s_dns_pkt[6] << 8) | s_dns_pkt[7];

    // Validate transaction ID
    if (rxId != txId) {
        result = NetDnsQueryStatus::MalformedResponse;
#if YORADIO_NET_DNS_DIAG
        Serial.printf("[NET_DNS] tag=%s resolver=%s result=malformed reason=txid_mismatch\n",
                      diagTag ? diagTag : "-", dnsServer.toString().c_str());
#endif
        xSemaphoreGive(mutex);
        return result;
    }

    // QR must be 1 (response), OPCODE must be 0 (standard query)
    if (!(flags & kDnsFlagQR) || (flags & kDnsFlagOpcode) != 0) {
        result = NetDnsQueryStatus::MalformedResponse;
        xSemaphoreGive(mutex);
        return result;
    }

    // TC=1 → truncated
    if (flags & kDnsFlagTC) {
#if YORADIO_NET_DNS_DIAG
        Serial.printf("[NET_DNS] tag=%s resolver=%s result=truncated\n",
                      diagTag ? diagTag : "-", dnsServer.toString().c_str());
#endif
        xSemaphoreGive(mutex);
        return NetDnsQueryStatus::Truncated;
    }

    // RCODE
    const uint8_t rcode = (uint8_t)(flags & kDnsRcodeMask);
    if (rcode != 0) {
#if YORADIO_NET_DNS_DIAG
        Serial.printf("[NET_DNS] tag=%s resolver=%s result=rcode=%u\n",
                      diagTag ? diagTag : "-", dnsServer.toString().c_str(), (unsigned)rcode);
#endif
        xSemaphoreGive(mutex);
        return NetDnsQueryStatus::ResponseError;
    }

    // Sanity-check question/answer counts
    if (qdcount > 4 || ancount > kMaxParsedRR) {
        xSemaphoreGive(mutex);
        return NetDnsQueryStatus::MalformedResponse;
    }

    // ── Skip question section ─────────────────────────────────────────────────
    size_t pos = 12;
    for (uint16_t q = 0; q < qdcount; ++q) {
        pos = skipDnsName(s_dns_pkt, pktLen, pos);
        if (pos == 0 || pos + 4 > pktLen) {
            xSemaphoreGive(mutex);
            return NetDnsQueryStatus::MalformedResponse;
        }
        pos += 4; // QTYPE + QCLASS
    }

    // ── Iterate answer records ────────────────────────────────────────────────
    uint8_t rrParsed = 0;
    for (uint16_t a = 0; a < ancount && rrParsed < kMaxParsedRR && outCount < outCapacity; ++a) {
        if (pos >= pktLen) break;

        // Skip owner name — handle compression pointer specifically so we can advance pos correctly.
        // Пропускаем owner name с правильной обработкой compressed pointer.
        size_t nameEnd = pos;
        {
            uint8_t jumps = kMaxPointerJumps;
            bool jumped = false;
            size_t follow = pos;
            while (follow < pktLen) {
                uint8_t lb = s_dns_pkt[follow];
                if (lb == 0) {
                    if (!jumped) nameEnd = follow + 1;
                    else if (nameEnd == pos) nameEnd = follow + 1; // shouldn't happen
                    break;
                } else if ((lb & 0xC0) == 0xC0) {
                    // Compression pointer
                    if (follow + 2 > pktLen) { pos = 0; break; }
                    if (!jumped) nameEnd = follow + 2; // advance packet pos past the pointer
                    if (jumps-- == 0) { pos = 0; break; }
                    const uint16_t target = (uint16_t)((lb & 0x3F) << 8) | s_dns_pkt[follow + 1];
                    if (target >= pktLen) { pos = 0; break; }
                    jumped = true;
                    follow = target;
                } else if ((lb & 0xC0) == 0) {
                    if (!jumped) nameEnd = follow + 1 + lb;
                    follow += 1 + lb;
                } else {
                    pos = 0; break; // reserved bits
                }
            }
            if (pos == 0) {
                xSemaphoreGive(mutex);
                return NetDnsQueryStatus::MalformedResponse;
            }
            pos = nameEnd;
        }

        if (pos + 10 > pktLen) break; // TYPE+CLASS+TTL+RDLENGTH need 10 bytes

        const uint16_t rrType     = ((uint16_t)s_dns_pkt[pos]   << 8) | s_dns_pkt[pos+1];
        const uint16_t rrClass    = ((uint16_t)s_dns_pkt[pos+2]  << 8) | s_dns_pkt[pos+3];
        // TTL at pos+4..pos+7 — skip
        const uint16_t rdLength   = ((uint16_t)s_dns_pkt[pos+8]  << 8) | s_dns_pkt[pos+9];
        pos += 10;

        if (pos + rdLength > pktLen) break; // bounds check before RDATA

        if (rrType == kDnsTypeA && rrClass == kDnsClassIN && rdLength == 4) {
            const uint32_t raw =
                ((uint32_t)s_dns_pkt[pos]   << 24) |
                ((uint32_t)s_dns_pkt[pos+1] << 16) |
                ((uint32_t)s_dns_pkt[pos+2] <<  8) |
                 (uint32_t)s_dns_pkt[pos+3];

            // Reject obviously invalid addresses (0.0.0.0, 255.255.255.255, loopback, multicast)
            const uint8_t firstOctet = s_dns_pkt[pos];
            const bool invalid = (raw == 0u) || (raw == 0xFFFFFFFFu) ||
                                 (firstOctet == 127) || (firstOctet >= 224);

            if (!invalid) {
                // IPAddress constructor from 4 bytes (big-endian)
                IPAddress ip(s_dns_pkt[pos], s_dns_pkt[pos+1], s_dns_pkt[pos+2], s_dns_pkt[pos+3]);
                const bool dup = isAlreadyInList(outAddresses, outCount, ip);
#if YORADIO_NET_DNS_DIAG
                Serial.printf("[NET_DNS] tag=%s resolver=%s A=%s dup=%d\n",
                              diagTag ? diagTag : "-",
                              dnsServer.toString().c_str(),
                              ip.toString().c_str(), (int)dup);
#endif
                if (!dup) {
                    outAddresses[outCount++] = ip;
                }
            }
        }
        pos += rdLength;
        ++rrParsed;
    }

    // ── Determine final status ────────────────────────────────────────────────
    result = (outCount > 0) ? NetDnsQueryStatus::Success : NetDnsQueryStatus::NoAddress;

#if YORADIO_NET_DNS_DIAG
    dns_log_heap("after", diagTag);
    Serial.printf("[NET_DNS] tag=%s resolver=%s status=%s a_count=%u elapsed_ms=%lu\n",
                  diagTag ? diagTag : "-",
                  dnsServer.toString().c_str(),
                  result == NetDnsQueryStatus::Success ? "ok" : "no_address",
                  (unsigned)outCount, (unsigned long)elapsed);
#endif

    xSemaphoreGive(mutex);
    return result;
}
