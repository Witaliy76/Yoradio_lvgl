#pragma once
#include <stdint.h>

// S07-4A.1: a byte position is not an acknowledgement of decoder/file teardown.
// S07-4A.1: позиция файла не подтверждает остановку декодера и закрытие ресурсов.
enum class AudioStopError : uint8_t { None, ControlTimeout, DecoderTimeout, DecoderTask, TransportClose };

struct AudioStopResult {
    bool decoderQuiesced = false;
    bool fileClosed = false;
    bool transportStopped = false;
    uint32_t position = 0;
    AudioStopError error = AudioStopError::None;

    bool ok() const {
        return error == AudioStopError::None && decoderQuiesced && fileClosed && transportStopped;
    }
    explicit operator bool() const { return ok(); }
};
