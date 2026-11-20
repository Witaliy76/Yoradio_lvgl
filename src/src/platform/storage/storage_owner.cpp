#include "storage_owner.h"
#include "storage_sdspi.h"
#include "storage_engineering_log.h"
#include <limits.h>

namespace yoradio { namespace storage {
namespace {
// Only metadata is protected. Driver/FS calls are always outside this lock.
struct Guard {
    portMUX_TYPE* lock;
    explicit Guard(portMUX_TYPE& mux) : lock(&mux) { portENTER_CRITICAL(lock); }
    ~Guard() { portEXIT_CRITICAL(lock); }
};
}

StorageOwner& StorageOwner::instance() { static StorageOwner owner; return owner; }
StorageStatus StorageOwner::status() const {
    Guard guard(lock_);
    StorageStatus copy = status_;
    copy.ready = admit(active_generation_);
    copy.generation = copy.ready ? active_generation_ : 0;
    if (!copy.ready) copy.free_bytes = copy.total_bytes = 0;
    copy.fault_pending = pending_error_ != StorageError::None;
    return copy;
}
bool StorageOwner::displayCommandPinsAvailable() const {
    const bool enabled = sdspi::bootConfig().enabled;
    Guard guard(lock_);
    return display_pins_available_ && (!enabled || early_called_);
}
bool StorageOwner::isLifecycleTask() const {
    Guard guard(lock_);
    return lifecycle_task_ && lifecycle_task_ == xTaskGetCurrentTaskHandle();
}
bool StorageOwner::admit(MediaGeneration generation) const {
    return admission_ && status_.state == StorageState::Mounted && generation != 0 &&
           active_generation_ == generation && pending_error_ == StorageError::None;
}

void StorageOwner::prepareBeforeDisplay() {
    const bool enabled = sdspi::bootConfig().enabled;
    {
        Guard guard(lock_);
        if (early_called_) return;
        early_called_ = true;
        lifecycle_task_ = xTaskGetCurrentTaskHandle();
        if (!enabled) return;
        status_.state = StorageState::Mounting;
        status_.error = StorageError::None;
        display_pins_available_ = false;
    }
    S07_STORAGE_LOG("[S07-ENG] early mount begin 400kHz\n");
    const auto early = sdspi::mount(400);
    // mount() already rolls back failures. Do not automatically retry a failed
    // cleanup (especially an IDF operation that may have destroyed its object).
    const auto released = early.error ? early : sdspi::cleanup();
    S07_STORAGE_LOG("[S07-ENG] early mount=%ld release=%ld held=%u Type9_handoff=%u\n",
        static_cast<long>(early.error),
        static_cast<long>(early.error ? early.cleanup_error : released.error),
        static_cast<unsigned>(released.resources_held),
        static_cast<unsigned>(!released.resources_held &&
            !(early.error ? early.cleanup_error : released.error)));
    Guard guard(lock_);
    resources_held_ = released.resources_held;
    status_.mounted = released.mounted;
    status_.native_error = early.error;
    status_.cleanup_error = early.error ? early.cleanup_error : released.error;
    early_ok_ = !early.error && !status_.cleanup_error && !resources_held_;
    display_pins_available_ = !resources_held_ && !status_.cleanup_error;
    status_.state = status_.cleanup_error ? StorageState::Fault :
        (early_ok_ || early.no_media ? StorageState::Absent : StorageState::Fault);
    status_.error = status_.cleanup_error ? StorageError::CleanupFailed :
        (early_ok_ ? StorageError::None :
         (early.no_media ? StorageError::NoMedia : StorageError::MountFailed));
}

void StorageOwner::mountAfterDisplay(bool display_ready) {
    if (!isLifecycleTask()) return;
    const bool enabled = sdspi::bootConfig().enabled;
    {
        Guard guard(lock_);
        if (runtime_called_) return;
        runtime_called_ = true;     // boot only; no retry/remount entry point
        if (!enabled || !early_ok_ || resources_held_) return;
        if (!display_ready) {
            status_.state = StorageState::Fault;
            status_.error = StorageError::DisplayNotReady;
            return;
        }
        display_pins_available_ = false; // never replay Type9 after runtime claim
        status_.state = StorageState::Mounting;
    }
    S07_STORAGE_LOG("[S07-ENG] Type9 ready; runtime mount begin requested=20000kHz host=SPI2\n");
    auto result = sdspi::mount(20000);
    // Try an intermediate SDSPI clock after FR_NO_FILESYSTEM and clean rollback.
    // После FR_NO_FILESYSTEM и успешного освобождения пробуем промежуточную частоту SDSPI.
    if (result.error == 13 && !result.cleanup_error && !result.resources_held && !result.mounted) {
        YORADIO_STORAGE_NOTE("[Storage] mount failed at 20 MHz, FatFs 13 FR_NO_FILESYSTEM; retry at 10 MHz\n");
        result = sdspi::mount(10000);
    }
    YORADIO_STORAGE_NOTE("[Storage] runtime mount real_khz=%d error=%ld mounted=%u\n",
        result.real_khz, static_cast<long>(result.error), static_cast<unsigned>(result.mounted));
    S07_STORAGE_LOG("[S07-ENG] runtime mount=%ld cleanup=%ld mounted=%u held=%u requested_khz=%d card_max_khz=%d real_khz=%d\n",
        static_cast<long>(result.error),static_cast<long>(result.cleanup_error),
        static_cast<unsigned>(result.mounted),static_cast<unsigned>(result.resources_held),
        result.requested_khz, result.card_max_khz, result.real_khz);
    Guard guard(lock_);
    resources_held_ = result.resources_held;
    status_.mounted = result.mounted;
    status_.native_error = result.error;
    status_.cleanup_error = result.cleanup_error;
    if (result.error || result.cleanup_error) {
        status_.state = result.no_media && !result.cleanup_error ?
            StorageState::Absent : StorageState::Fault;
        status_.error = result.cleanup_error ? StorageError::CleanupFailed :
            (result.no_media ? StorageError::NoMedia : StorageError::MountFailed);
        return;
    }
    if (sequence_ == UINT64_MAX) {
        status_.state = StorageState::Fault;
        status_.error = StorageError::GenerationExhausted;
        return;                   // fail closed; explicit drained teardown remains possible
    }
    active_generation_ = ++sequence_;
    handle_generation_ = active_generation_;
    status_.state = StorageState::Mounted;
    status_.error = StorageError::None;
    status_.free_bytes = result.free_bytes;
    status_.total_bytes = result.total_bytes;
    admission_ = true;
}

bool StorageOwner::acquireOperation(MediaGeneration expected, OperationLease& out) {
    Guard guard(lock_);
    if (out.owner_ || !admit(expected) || status_.in_flight == UINT32_MAX) return false;
    ++status_.in_flight;
    out.owner_ = this;
    out.generation_ = expected;
    return true;
}
bool StorageOwner::reserveHandle(const OperationLease& operation, HandleLease& out) {
    Guard guard(lock_);
    if (out.owner_ || operation.owner_ != this || !admit(operation.generation_) ||
        status_.open_handles == UINT32_MAX) return false;
    ++status_.open_handles;
    out.owner_ = this;
    out.generation_ = operation.generation_;
    return true;
}
bool StorageOwner::acquireOperation(const HandleLease& handle, OperationLease& out) {
    if (handle.owner_ != this) return false;
    return acquireOperation(handle.generation_, out);
}
bool StorageOwner::acquireClose(HandleLease& handle, CloseLease& out) {
    Guard guard(lock_);
    if (out.owner_ || handle.owner_ != this || handle.generation_ == 0 ||
        handle.generation_ != handle_generation_ || !status_.mounted ||
        status_.open_handles == 0 || status_.in_flight == UINT32_MAX) return false;
    ++status_.in_flight;
    out.owner_ = this;
    out.generation_ = handle.generation_;
    handle.owner_ = nullptr;        // one owner for this reservation, including moves
    return true;
}
void StorageOwner::releaseOperation() { Guard guard(lock_); --status_.in_flight; }
bool StorageOwner::releaseHandle(MediaGeneration generation) {
    Guard guard(lock_);
    if (!generation || generation != handle_generation_ || !status_.open_handles) return false;
    --status_.open_handles;
    return true;
}
bool StorageOwner::completeClose(MediaGeneration generation) {
    Guard guard(lock_);
    if (!generation || generation != handle_generation_ ||
        !status_.open_handles || !status_.in_flight) return false;
    --status_.open_handles;
    --status_.in_flight;
    return true;
}
void StorageOwner::reportMediaFault(MediaGeneration expected, int32_t error) {
    Guard guard(lock_);
    if (!admit(expected)) return;  // stale reports cannot poison a different mount
    admission_ = false;
    pending_error_ = StorageError::MediaFault;
    pending_native_ = error;
}
void StorageOwner::abandonHandle(MediaGeneration generation) {
    Guard guard(lock_);
    // Losing a lease without confirmed close cannot prove drained. Keep its
    // count permanently, latch a fault, and never close a foreign File/DIR.
    if (generation != 0 && generation == handle_generation_) {
        admission_ = false;
        // Diagnose abandonment even after quiesce/fault invalidates admission,
        // or while a media fault is pending. Preserve the underlying IO code.
        if (pending_error_ == StorageError::None) pending_native_ = status_.native_error;
        pending_error_ = StorageError::UnclosedHandle;
    }
}
void StorageOwner::processFault() {
    if (!isLifecycleTask()) return;
    Guard guard(lock_);
    if (pending_error_ == StorageError::None) return;
    status_.state = StorageState::Fault;
    status_.error = pending_error_;
    status_.native_error = pending_native_;
    pending_error_ = StorageError::None;
    active_generation_ = 0;
}
bool StorageOwner::refreshSpace() {
    if (!isLifecycleTask()) return false;
    const auto before = status();
    OperationLease operation;
    if (!acquireOperation(before.generation, operation)) return false;
    uint64_t free = 0, total = 0;
    const auto error = sdspi::space(free, total);
    if (error) { reportMediaFault(before.generation, error); return false; }
    Guard guard(lock_);
    if (!admit(before.generation)) return false;
    status_.free_bytes = free;
    status_.total_bytes = total;
    return true;
}
bool StorageOwner::beginQuiesce() {
    if (!isLifecycleTask()) return false;
    processFault();
    Guard guard(lock_);
    if (status_.state == StorageState::Disabled) return true;
    admission_ = false;
    active_generation_ = 0;
    status_.state = StorageState::Quiescing;
    return true;
}
bool StorageOwner::isDrained() const {
    Guard guard(lock_);
    return status_.in_flight == 0 && status_.open_handles == 0;
}
bool StorageOwner::unmountWhenDrained() {
    if (!isLifecycleTask()) return false;
    {
        Guard guard(lock_);
        if (status_.state == StorageState::Disabled) return true;
        if (status_.state != StorageState::Quiescing || admission_ ||
            status_.in_flight || status_.open_handles) return false;
    }
    const auto result = sdspi::cleanup();
    Guard guard(lock_);
    resources_held_ = result.resources_held;
    status_.mounted = result.mounted;
    status_.free_bytes = status_.total_bytes = 0;
    if (result.error || resources_held_) {
        status_.state = StorageState::Fault;
        status_.error = StorageError::CleanupFailed;
        status_.cleanup_error = result.error;
        return false;
    }
    status_.state = StorageState::Absent;
    handle_generation_ = 0;
    return true;
}

OperationLease::~OperationLease() { release(); }
OperationLease::OperationLease(OperationLease&& other) noexcept
    : owner_(other.owner_), generation_(other.generation_) { other.owner_ = nullptr; }
void OperationLease::release() {
    if (owner_) { owner_->releaseOperation(); owner_ = nullptr; }
}
HandleLease::~HandleLease() { if (owner_) owner_->abandonHandle(generation_); }
HandleLease::HandleLease(HandleLease&& other) noexcept
    : owner_(other.owner_), generation_(other.generation_) { other.owner_ = nullptr; }
void HandleLease::releaseAfterClose() {
    if (owner_ && owner_->releaseHandle(generation_)) owner_ = nullptr;
}
CloseLease::~CloseLease() {
    if (owner_) {
        owner_->abandonHandle(generation_);
        owner_->releaseOperation(); // close attempt ended, handle closure remains unknown
    }
}
CloseLease::CloseLease(CloseLease&& other) noexcept
    : owner_(other.owner_), generation_(other.generation_) { other.owner_ = nullptr; }
bool CloseLease::confirmClosed() {
    if (!owner_ || !owner_->completeClose(generation_)) return false;
    owner_ = nullptr;
    return true;
}
}} // namespace yoradio::storage
