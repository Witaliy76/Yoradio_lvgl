// Engineering parser for the gated s07act telnet/serial command.
// It admits or reads. It does not play, stop, scan, or poll.
// Инженерный разбор gated-команды s07act для telnet/serial.
// Команда принимает запрос или читает снимок. Она не играет, не останавливает, не сканирует и не опрашивает.
#pragma once
#include "source_catalog.h"
#include <stdio.h>
#include <string.h>

namespace yoradio { namespace s07act {

enum class Verb : uint8_t { None = 0, Radio, Sd, Status };

struct Command {
    bool matched = false;
    Verb verb = Verb::None;
};

// Exact "s07act" plus one token: radio, sd, or status.
// Точное "s07act" и один токен: radio, sd или status.
inline Command parse(const char* line) {
    Command cmd;
    if (!line || strncmp(line, "s07act", 6) != 0) return cmd;
    if (line[6] != '\0' && line[6] != ' ') return cmd;
    cmd.matched = true;
    if (line[6] != ' ' || line[7] == '\0' || strchr(line + 7, ' ') != nullptr) return cmd;
    const char* arg = line + 7;
    if (strcmp(arg, "radio") == 0) cmd.verb = Verb::Radio;
    else if (strcmp(arg, "sd") == 0) cmd.verb = Verb::Sd;
    else if (strcmp(arg, "status") == 0) cmd.verb = Verb::Status;
    return cmd;
}

inline const char* outcomeName(source::ActivateOutcome outcome) {
    using source::ActivateOutcome;
    switch (outcome) {
    case ActivateOutcome::Pending: return "Pending";
    case ActivateOutcome::SelectedStopped: return "SelectedStopped";
    case ActivateOutcome::Playing: return "Playing";
    case ActivateOutcome::AlreadyActive: return "AlreadyActive";
    case ActivateOutcome::NeedsSelection: return "NeedsSelection";
    case ActivateOutcome::Unavailable: return "Unavailable";
    case ActivateOutcome::StaleMedia: return "StaleMedia";
    case ActivateOutcome::StopFailed: return "StopFailed";
    case ActivateOutcome::OpenFailed: return "OpenFailed";
    case ActivateOutcome::Cancelled: return "Cancelled";
    case ActivateOutcome::Superseded: return "Superseded";
    case ActivateOutcome::Busy: return "Busy";
    default: return "Cancelled";
    }
}

// Confirmed playback word. Values match PlaybackState.
// Слово подтверждённого воспроизведения. Значения совпадают с PlaybackState.
enum class PlayWord : uint8_t { Stopped = 0, Playing = 1, StopFailed = 2 };

inline const char* playWord(PlayWord state) {
    if (state == PlayWord::Playing) return "Playing";
    if (state == PlayWord::StopFailed) return "StopFailed";
    return "Stopped";
}

inline const char* sourceWord(bool valid, source::SourceId id) {
    if (!valid) return "None";
    if (id == source::SourceId::Radio) return "radio";
    if (id == source::SourceId::Sd) return "sd";
    return "None";
}

struct View {
    uint32_t request = 0;
    source::ActivateOutcome outcome = source::ActivateOutcome::Cancelled;
    bool complete = false;
    bool selected = false;
    source::SourceId selected_id = source::SourceId::Radio;
    bool sounding = false;
    source::SourceId sounding_id = source::SourceId::Radio;
    PlayWord state = PlayWord::Stopped;
    uint32_t epoch = 0;
};

inline int formatAdmit(char* dst, size_t cap, const View& view) {
    if (!dst || !cap) return -1;
    return snprintf(dst, cap,
        "s07act admit request=%u outcome=%s selected=%s sounding=%s state=%s epoch=%u",
        static_cast<unsigned>(view.request), outcomeName(view.outcome),
        sourceWord(view.selected, view.selected_id),
        sourceWord(view.sounding, view.sounding_id),
        playWord(view.state), static_cast<unsigned>(view.epoch));
}

inline int formatStatus(char* dst, size_t cap, const View& view) {
    if (!dst || !cap) return -1;
    return snprintf(dst, cap,
        "s07act status request=%u outcome=%s complete=%u selected=%s sounding=%s state=%s epoch=%u",
        static_cast<unsigned>(view.request), outcomeName(view.outcome),
        view.complete ? 1u : 0u,
        sourceWord(view.selected, view.selected_id),
        sourceWord(view.sounding, view.sounding_id),
        playWord(view.state), static_cast<unsigned>(view.epoch));
}

// admit performs one nonblocking admission. read only copies a snapshot.
// Neither hook is a loop, a scan, or a file open. Status never calls admit.
// admit — один приём без ожидания. read только копирует снимок.
// Ни один хук не является loop, сканированием или открытием файла. status не вызывает admit.
struct Ports {
    void* ctx = nullptr;
    void (*admit)(void* ctx, source::SourceId id, View& out) = nullptr;
    void (*read)(void* ctx, View& out) = nullptr;
};

enum class Effect : uint8_t { Ignored, Usage, Admitted, Read };

inline Effect apply(const char* line, const Ports& ports, View& out) {
    const Command cmd = parse(line);
    if (!cmd.matched) return Effect::Ignored;
    if (cmd.verb == Verb::Status) {
        if (ports.read) ports.read(ports.ctx, out);
        return Effect::Read;
    }
    if (cmd.verb == Verb::None) return Effect::Usage;
    if (ports.admit) {
        ports.admit(ports.ctx, cmd.verb == Verb::Radio ? source::SourceId::Radio
                                                       : source::SourceId::Sd, out);
    }
    return Effect::Admitted;
}

}}
