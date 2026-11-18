// Player command payload and playback policy; Storage owns only media identity.
// Данные команды и правила воспроизведения; Storage хранит только путь/поколение.
#pragma once
#include "../platform/storage/media_ref.h"
#include "../audioI2S/audio_text_url_utils.h"

namespace yoradio { namespace playback {
enum class PathPlayResult : uint8_t {
    None, Accepted, Busy, NotReady, InvalidPath, PathTooLong, UnsupportedFormat,
    StaleMedia, StaleEpoch, SourceBusy, StopFailed, OpenFailed, IoError,
    Playing, Stopped, EndOfFile, NoAdjacentTrack, NoMedia, ReadFailed, SeekFailed,
    PathRequired, SearchingNext, ShuffleLimit, NoMemory
};

// RAM-only SD play modes. Player owns them; Radio ignores them.
// Режимы SD только в RAM. Владеет Player; Radio их не использует.
enum class SdRepeatMode : uint8_t { Off = 0, One = 1, Folder = 2, All = 3 };

enum class PlaybackSource : uint8_t { Radio, SingleFile };
enum class PlaybackState : uint8_t { Stopped, Playing, StopFailed };
// Last confirmed source; an accepted request does not change this snapshot.
// Последний подтверждённый источник; приём запроса не меняет этот snapshot.
struct PlaybackContext {
    PlaybackSource source = PlaybackSource::Radio;
    PlaybackState state = PlaybackState::Stopped;
    uint32_t epoch = 1;
    uint64_t generation = 0;
    uint16_t radioStation = 0;
};

inline PathPlayResult pathResult(storage::FileError error) {
    switch(error) {
    case storage::FileError::None: return PathPlayResult::None;
    case storage::FileError::PathTooLong: return PathPlayResult::PathTooLong;
    case storage::FileError::InvalidPath: return PathPlayResult::InvalidPath;
    case storage::FileError::StaleMedia: return PathPlayResult::StaleMedia;
    default: return PathPlayResult::IoError;
    }
}

inline PathPlayResult validatePathPlayback(const char* path) {
    const auto result = pathResult(storage::validateMediaPath(path));
    if(result != PathPlayResult::None) return result;
    return audio_safe::endsWithIcase(path,".mp3") || audio_safe::endsWithIcase(path,".flac")
        ? PathPlayResult::None : PathPlayResult::UnsupportedFormat;
}

struct PathPlayRequest {
    storage::MediaRef media;
    uint32_t epoch = 0;

    static PathPlayResult copy(const char* path, uint64_t generation, uint32_t epoch,
                               PathPlayRequest& out) {
        const auto result = validatePathPlayback(path);
        if(result != PathPlayResult::None) return result;
        if(!epoch) return PathPlayResult::StaleEpoch;
        const auto error = storage::MediaRef::copy(path,generation,out.media);
        if(error != storage::FileError::None) return pathResult(error);
        out.epoch = epoch;
        return PathPlayResult::None;
    }
};
}}
