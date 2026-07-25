#ifndef net_dns_resolver_h
#define net_dns_resolver_h

/*
 * Generic selected-DNS A-record resolver.
 * Resolves a hostname by querying a single caller-specified IPv4 DNS server
 * via bounded synchronous UDP/53, without touching global lwIP DNS config.
 *
 * Threading: the static 512-byte packet buffer is guarded by a helper-owned
 * mutex, so the helper is safe to call from different tasks sequentially —
 * one query at a time.  Must not be called from an ISR.
 *
 * HTTPS/SNI note: this helper returns raw IPv4 candidates only. A future
 * HTTPS consumer must separately handle SNI and certificate hostname
 * verification for the original hostname — this resolver does not solve that.
 *
 * Responsibility boundary: no Weather, no HTTP, no scheduler, no API keys.
 * — только разрешение имён; без погоды, HTTP, ключей API.
 *
 * Author: Witaliy76 - https://github.com/Witaliy76
 */

#include <cstdint>
#include <cstddef>
#include <IPAddress.h>

// ── DNS-server descriptor ────────────────────────────────────────────────────
// Used by callers to identify servers and label diagnostic output.
// Используется вызывающим для идентификации сервера и меток в диагностике.
struct NetDnsServer {
    const char* name;    // human-readable name for diagnostics ("cloudflare", "quad9")
    IPAddress   address; // resolver IPv4 address
};

// ── Query result status ──────────────────────────────────────────────────────
enum class NetDnsQueryStatus : uint8_t {
    Success,          // ≥1 unique A record returned in outAddresses
    InvalidArgument,  // null hostname, empty hostname, or null outAddresses
    LockUnavailable,  // mutex creation or acquisition failed
    UdpStartFailed,   // WiFiUDP::begin() or beginPacket() failed
    SendFailed,       // endPacket() returned 0
    Timeout,          // no valid response from the selected resolver within timeoutMs
    MalformedResponse,// packet too short, header inconsistent, offset out of bounds
    ResponseError,    // RCODE != 0 (e.g. NXDOMAIN=3, SERVFAIL=2)
    Truncated,        // TC=1 in response; no TCP fallback in this version
    NoAddress,        // valid DNS response but no usable A/IN records (only CNAMEs etc.)
};

// ── Main query function ──────────────────────────────────────────────────────
/*
 * Query `dnsServer` for A records of `hostname`.
 * Up to `outCapacity` unique IPv4 results are written to `outAddresses`.
 * `outCount` is set deterministically on all paths (including error paths: set to 0).
 * `diagnosticTag` is an optional caller label for log messages (e.g. "weather-current").
 * — caller owns outAddresses storage; no dynamic allocation, no exceptions.
 */
NetDnsQueryStatus netDnsQueryA(
    const char*      hostname,
    const IPAddress& dnsServer,
    IPAddress*       outAddresses,
    size_t           outCapacity,
    size_t&          outCount,
    uint32_t         timeoutMs,
    const char*      diagnosticTag = nullptr);

// ── Compiled default fallback-server accessor ────────────────────────────────
/*
 * Returns the compiled-in fallback DNS list (Cloudflare + Quad9).
 * Weather and other callers pass this list (or a future runtime-configured
 * list) to netDnsQueryA; the accessor is never hard-wired into weather policy.
 * `count` is set to the number of entries.
 * — compiled defaults; future WebUI overrides go through a separate accessor
 *   that returns runtime values or falls back to these defaults.
 */
const NetDnsServer* netDnsDefaultFallbackServers(size_t& count);

#endif // net_dns_resolver_h
