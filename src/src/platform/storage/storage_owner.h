// One mount/lifecycle owner; handle IO stays with the task holding its lease.
// Единый владелец монтирования; IO выполняет задача, владеющая дескриптором.
#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace yoradio { namespace storage {

using MediaGeneration = uint64_t;  // mount incarnation, never a card identity
enum class StorageState : uint8_t { Disabled, Absent, Mounting, Mounted, Quiescing, Fault };
enum class StorageError : uint8_t {
    None, ProductGate, NoMedia, MountFailed, CleanupFailed, DisplayNotReady,
    MediaFault, UnclosedHandle, GenerationExhausted
};
struct StorageStatus {
    StorageState state = StorageState::Disabled;
    MediaGeneration generation = 0;  // zero when admission is closed
    StorageError error = StorageError::ProductGate;
    int32_t native_error = 0;
    int32_t cleanup_error = 0;
    bool mounted = false;           // owned FAT mount, not physical presence proof
    bool ready = false;
    bool fault_pending = false;
    uint64_t free_bytes = 0;        // cached as one pair; refresh only by owner
    uint64_t total_bytes = 0;
    uint32_t in_flight = 0;
    uint32_t open_handles = 0;
};

class StorageOwner;
class CloseLease;
// Counts admitted synchronous IO until it returns; does not impose a deadline.
// Учитывает допущенный синхронный IO до возврата; время выполнения не ограничивает.
class OperationLease {
public:
    OperationLease() = default;
    ~OperationLease();
    OperationLease(const OperationLease&) = delete;
    OperationLease& operator=(const OperationLease&) = delete;
    OperationLease(OperationLease&& other) noexcept;
    OperationLease& operator=(OperationLease&&) = delete;
    void release();                 // only AFTER the synchronous FS call returns
private:
    friend class StorageOwner;
    StorageOwner* owner_ = nullptr;
    MediaGeneration generation_ = 0;
};

class HandleLease {
public:
    HandleLease() = default;
    ~HandleLease();
    HandleLease(const HandleLease&) = delete;
    HandleLease& operator=(const HandleLease&) = delete;
    HandleLease(HandleLease&& other) noexcept;
    HandleLease& operator=(HandleLease&&) = delete;
    // Reserve BEFORE open. On open failure, or AFTER actual File/DIR close,
    // release the reservation. No File/DIR is closed by StorageOwner.
    void releaseAfterClose();
private:
    friend class StorageOwner;
    StorageOwner* owner_ = nullptr;
    MediaGeneration generation_ = 0;
};

// Owns an already-counted handle while its owning task performs close only.
// Cannot be passed to acquireOperation/reserveHandle. Acquire only after earlier
// calls on this handle return. Tokens must not be accessed concurrently.
// Учитывает закрытие уже открытого дескриптора даже при запрете новых операций.
// Получать после завершения его IO; подтверждать только фактическое закрытие.
class CloseLease {
public:
    CloseLease() = default;
    ~CloseLease();
    CloseLease(const CloseLease&) = delete;
    CloseLease& operator=(const CloseLease&) = delete;
    CloseLease(CloseLease&& other) noexcept;
    CloseLease& operator=(CloseLease&&) = delete;
    // Call only after actual close is confirmed. An unknown close result must
    // not be acknowledged: destruction retains the handle count and faults.
    bool confirmClosed();          // true exactly once; does not itself close IO
private:
    friend class StorageOwner;
    StorageOwner* owner_ = nullptr;
    MediaGeneration generation_ = 0;
};

class StorageOwner {
public:
    static StorageOwner& instance();
    StorageOwner(const StorageOwner&) = delete;
    StorageOwner& operator=(const StorageOwner&) = delete;

    // Called once by setup task, before Type9 / after confirmed display init.
    // No-op under the product gate, including probe builds.
    void prepareBeforeDisplay();
    void mountAfterDisplay(bool display_ready);
    bool displayCommandPinsAvailable() const;
    StorageStatus status() const;

    bool acquireOperation(MediaGeneration expected, OperationLease& out);
    bool reserveHandle(const OperationLease& operation, HandleLease& out);
    bool acquireOperation(const HandleLease& handle, OperationLease& out);
    // Transfers the handle reservation to a counted close-only call, even after
    // admission/generation invalidation. The input token becomes empty.
    bool acquireClose(HandleLease& handle, CloseLease& out);

    // Cross-task signal only. Closes admission immediately; does not tear down
    // resources. The lifecycle owner consumes the signal in processFault().
    void reportMediaFault(MediaGeneration expected, int32_t native_error);
    void processFault();
    bool refreshSpace();
    bool beginQuiesce();
    bool isDrained() const;
    bool unmountWhenDrained();       // false=busy/error; never waits for a client

private:
    StorageOwner() = default;
    friend class OperationLease;
    friend class HandleLease;
    friend class CloseLease;
    void releaseOperation();
    bool releaseHandle(MediaGeneration generation);
    bool completeClose(MediaGeneration generation);
    void abandonHandle(MediaGeneration generation);
    bool isLifecycleTask() const;
    bool admit(MediaGeneration generation) const; // caller holds lock_

    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    StorageStatus status_;
    TaskHandle_t lifecycle_task_ = nullptr;
    MediaGeneration sequence_ = 0;
    MediaGeneration active_generation_ = 0;
    // Resource lifetime survives admission invalidation; cleared only after
    // confirmed drained teardown. Never admits open/read/seek.
    MediaGeneration handle_generation_ = 0;
    bool admission_ = false;
    bool early_called_ = false;
    bool early_ok_ = false;
    bool runtime_called_ = false;
    bool resources_held_ = false;
    bool display_pins_available_ = true;
    StorageError pending_error_ = StorageError::None;
    int32_t pending_native_ = 0;
};

}} // namespace yoradio::storage
