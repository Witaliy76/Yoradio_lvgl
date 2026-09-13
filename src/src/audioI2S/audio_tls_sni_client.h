#ifndef AUDIO_TLS_SNI_CLIENT_H
#define AUDIO_TLS_SNI_CLIENT_H

#include <stdint.h>

// TLS client wrapper that keeps the original hostname when connecting to an
// address produced by the project's own DNS resolver.
// Обёртка TLS-клиента: соединяемся по IP из собственного DNS resolver, но
// сохраняем исходный hostname для TLS.
//
// Why / Зачем:
//   The Arduino core's connect(IPAddress, port) forwards host = NULL into
//   start_ssl_client(), and mbedtls_ssl_set_hostname() then falls back to
//   ip.toString(). SNI and any name checking therefore see an IP literal
//   instead of the stream's hostname. The only core API that accepts a
//   pre-resolved IP together with the original hostname is the public
//   6-argument overload
//     connect(IPAddress, uint16_t, const char* host,
//             const char* CA_cert, const char* cert, const char* private_key)
//   (Arduino-ESP32 core 3.3.11, NetworkClientSecure).
//   connect(IPAddress, port) в ядре передаёт host = NULL, и mbedtls использует
//   строковый IP. Единственный публичный API с готовым IP и hostname — этот
//   6-аргументный overload.
//
// The wrapper deliberately forwards exactly the credentials the base class
// would have used for connect(ip, port), so setInsecure() / CA bundle / ALPN /
// timeout policy are unchanged — only the hostname is added.
// Обёртка передаёт ровно те же credentials, что базовый connect(ip, port), —
// политика проверки сертификатов, ALPN и таймауты не меняются.
//
// Templated on the base only so the contract can be exercised against a mock
// TLS base in a host build; production instantiates it with the core class.
// Шаблон по базовому классу нужен только для проверки контракта на mock-базе.
template <class TlsBase>
class AudioTlsSniClientT : public TlsBase {
public:
    template <class IpT>
    int connectWithSni(const IpT& ip, uint16_t port, const char* hostname) {
        // PSK mode has no IP+hostname overload in the core — leave it as is.
        // Для PSK в ядре нет overload с IP+hostname — режим не трогаем.
        if(this->_pskIdent && this->_psKey) {
            return TlsBase::connect(ip, port, this->_pskIdent, this->_psKey);
        }
        return TlsBase::connect(ip, port, hostname,
                                this->_CA_cert, this->_cert, this->_private_key);
    }
};

#endif
