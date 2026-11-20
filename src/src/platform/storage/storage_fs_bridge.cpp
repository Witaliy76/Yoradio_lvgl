#include "storage_fs_bridge.h"
#include "storage_owner.h"
#include "storage_sdspi.h"
#include "storage_engineering_log.h"
#include <FSImpl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <new>

namespace yoradio { namespace storage {
namespace {
struct Binding {
    std::shared_ptr<StorageFileStatus> status{new (std::nothrow) StorageFileStatus};
    FSMutexHandle mutex = fsMutexCreate();
    uint64_t generation = 0;
    ~Binding() { fsLockDelete(mutex); }
    bool usable() const { return status && (!fsMutexEnabled() || mutex); }
    void fail(FileError error, int native = 0, bool media = false) {
        if (!status) return;
        status->native_error.store(native);
        status->error.store(error);
        YORADIO_STORAGE_NOTE("[Storage] file error=%u native=%d\n",
            static_cast<unsigned>(error), native);
        if (media) StorageOwner::instance().reportMediaFault(generation,native);
    }
};
bool mediaError(int e) { return e == EIO || e == ENODEV || e == ETIMEDOUT; }

class GuardedFile final : public fs::FileImpl {
public:
    std::shared_ptr<Binding> binding;
    HandleLease handle;
    int fd = -1;
    bool reserved = false;
    size_t position_ = 0, size_ = 0;
    time_t mtime_ = 0;
    char path_[kMaxMediaPathBytes+1]{};
    GuardedFile(std::shared_ptr<Binding> b,const char* p) : binding(std::move(b)) {
        memcpy(path_,p,strlen(p)+1);
    }
    ~GuardedFile() override { close(); }
    bool operation(OperationLease& op) {
        if (fd >= 0 && StorageOwner::instance().acquireOperation(handle,op)) return true;
        binding->fail(FileError::StaleMedia);
        return false;
    }
    size_t write(const uint8_t*,size_t) override { binding->fail(FileError::ReadOnly);return 0; }
    size_t read(uint8_t* out,size_t count) override {
        fs::FSLockGuard lock(binding->mutex);
        OperationLease op;
        if (!operation(op) || binding->status->failed()) return 0;
        if (!count) return 0;
        if (!out) { binding->fail(FileError::ReadFailed,EINVAL);return 0; }
        errno=0;
        const auto n=::read(fd,out,count);
        const int error=errno;
        if (n < 0 || error || (n == 0 && position_ < size_)) {
            const int e=error ? error : EIO;
            binding->fail(FileError::ReadFailed,e,mediaError(e));
            return 0;
        }
        position_+=static_cast<size_t>(n);
        binding->status->eof.store(position_ == size_);
        return static_cast<size_t>(n);
    }
    void flush() override {} // read-only, no pending writes
    bool seek(uint32_t position,fs::SeekMode mode) override {
        fs::FSLockGuard lock(binding->mutex);
        OperationLease op;
        if (!operation(op) || binding->status->failed()) return false;
        uint64_t target=position;
        if (mode == fs::SeekCur) target+=position_;
        else if (mode == fs::SeekEnd) target+=size_;
        else if (mode != fs::SeekSet) { binding->fail(FileError::SeekFailed,EINVAL);return false; }
        if (target > size_ || target > INT32_MAX) {
            binding->fail(FileError::SeekFailed,EINVAL);return false;
        }
        errno=0;
        const auto pos=::lseek(fd,static_cast<off_t>(target),SEEK_SET);
        const int error=errno;
        if (pos < 0 || error || static_cast<uint64_t>(pos) != target) {
            binding->fail(FileError::SeekFailed,error ? error : EIO,mediaError(error));return false;
        }
        position_=static_cast<size_t>(pos);binding->status->eof.store(position_ == size_);return true;
    }
    size_t position() const override { return position_; }
    size_t size() const override { return size_; }
    bool setBufferSize(size_t) override { return false; } // native unbuffered fd
    void close() override {
        fs::FSLockGuard lock(binding->mutex);
        if (!reserved) return;
        CloseLease close;
        if (!StorageOwner::instance().acquireClose(handle,close)) {
            binding->fail(FileError::CloseFailed);return;
        }
        // IDF FAT close may discard descriptor state on error. Never retry a
        // possibly-reused fd; an unknown close remains counted/fail-closed.
        const int old_fd=fd;fd=-1;reserved=false;
        errno=0;
        if (old_fd >= 0 && ::close(old_fd) != 0) {
            binding->fail(FileError::CloseFailed,errno,mediaError(errno));return;
        }
        // fd==-1 here is a proven failed open reservation, not a guessed close.
        if (!close.confirmClosed()) { binding->fail(FileError::CloseFailed);return; }
        binding->status->handles.fetch_sub(1);
    }
    time_t getLastWrite() override { return mtime_; }
    const char* path() const override { return path_; }
    const char* name() const override { return strrchr(path_,'/')+1; }
    boolean isDirectory() override { return false; } // open rejects nonregular files
    fs::FileImplPtr openNextFile(const char*) override { return {}; }
    boolean seekDir(long) override { return false; }
    String getNextFileName() override { return {}; }
    String getNextFileName(bool* dir) override { if(dir)*dir=false;return {}; }
    void rewindDirectory() override {}
    operator bool() override { return fd >= 0; }
};

class BridgeImpl final : public fs::FSImpl {
public:
    std::shared_ptr<Binding> binding{new (std::nothrow) Binding};
    fs::FileImplPtr open(const char* path,const char* mode,const bool create) override {
        if (!binding || !binding->usable()) return {};
        // Arduino FS uses a recursive mutex: error cleanup and File destruction
        // may call GuardedFile::close() while this same task still holds the lock.
        // Рекурсивный mutex допускает close при обработке ошибки под этой блокировкой.
        fs::FSLockGuard lock(binding->mutex);
        if (binding->status->handles.load()) return {}; // one physical file per binding
        binding->status->error.store(FileError::None);binding->status->eof.store(false);
        const auto error=validateMediaPath(path);
        if (error != FileError::None) { binding->fail(error);return {}; }
        if (create || !mode || strcmp(mode,"r")) { binding->fail(FileError::ReadOnly);return {}; }
        OperationLease operation;
        auto& owner=StorageOwner::instance();
        if (!owner.acquireOperation(binding->generation,operation)) { binding->fail(FileError::StaleMedia);return {}; }
        std::shared_ptr<GuardedFile> file(new (std::nothrow) GuardedFile(binding,path));
        if (!file) { binding->fail(FileError::NoMemory);return {}; }
        if (!owner.reserveHandle(operation,file->handle)) { binding->fail(FileError::StaleMedia);return {}; }
        file->reserved=true;binding->status->handles.fetch_add(1);
        char physical[kMaxMediaPathBytes+65];
        const char* root=sdspi::bootConfig().path;
        const size_t root_length=root ? strnlen(root,64) : 64;
        if (root_length==64 || !root_length || root[0]!='/') {
            binding->fail(FileError::InvalidPath);file->close();return {};
        }
        memcpy(physical,root,root_length);memcpy(physical+root_length,path,strlen(path)+1);
        g48("D-before-open");
        errno=0;file->fd=::open(physical,O_RDONLY);
        if (file->fd < 0) { const int e=errno;binding->fail(FileError::OpenFailed,e,mediaError(e));file->close();return {}; }
        struct stat info{};
        if (::fstat(file->fd,&info) != 0) {
            const int e=errno;
            binding->fail(e == EOVERFLOW ? FileError::FileTooLarge : FileError::OpenFailed,e,mediaError(e));
            file->close();return {};
        }
        if (!S_ISREG(info.st_mode)) { binding->fail(FileError::NotRegularFile);file->close();return {}; }
        // Existing Audio uses signed 32-bit seek/resume offsets.
        if (info.st_size < 0 || static_cast<uint64_t>(info.st_size)>INT32_MAX) {
            binding->fail(FileError::FileTooLarge);file->close();return {};
        }
        file->size_=static_cast<size_t>(info.st_size);file->mtime_=info.st_mtime;
        OperationLease still_current;
        if (!owner.acquireOperation(binding->generation,still_current)) {
            binding->fail(FileError::StaleMedia);file->close();return {};
        }
        return file;
    }
    bool exists(const char* path) override {
        auto file=open(path,"r",false);if(!file)return false;file->close();return !binding->status->failed();
    }
    bool rename(const char*,const char*) override { return false; }
    bool remove(const char*) override { return false; }
    bool mkdir(const char*) override { return false; }
    bool rmdir(const char*) override { return false; }
};
}
MediaProbe probeMediaFile(uint64_t generation, const char* path) {
    // A missing file must not fault the mount or the file that is already playing.
    // Отсутствующий файл не должен переводить mount или уже играющий файл в fault.
    if (!generation || validateMediaPath(path) != FileError::None) return MediaProbe::Io;
    auto& owner = StorageOwner::instance();
    const auto status = owner.status();
    if (!status.mounted && !status.ready) return MediaProbe::Unavailable;
    if (!status.ready || status.generation != generation) return MediaProbe::Stale;
    OperationLease operation;
    if (!owner.acquireOperation(generation, operation)) return MediaProbe::Stale;
    HandleLease handle;
    if (!owner.reserveHandle(operation, handle)) return MediaProbe::Stale;
    char physical[kMaxMediaPathBytes + 65];
    const char* root = sdspi::bootConfig().path;
    const size_t root_length = root ? strnlen(root, 64) : 64;
    const size_t path_length = strlen(path);
    if (root_length == 64 || !root_length || root[0] != '/' ||
        root_length + path_length + 1 > sizeof(physical)) {
        handle.releaseAfterClose();
        return MediaProbe::Io;
    }
    memcpy(physical, root, root_length);
    memcpy(physical + root_length, path, path_length + 1);
    errno = 0;
    const int fd = ::open(physical, O_RDONLY);
    if (fd < 0) {
        const int error = errno;
        handle.releaseAfterClose();
        return (error == ENOENT || error == ENOTDIR) ? MediaProbe::Missing : MediaProbe::Io;
    }
    struct stat info{};
    const bool stated = ::fstat(fd, &info) == 0;
    const int stat_error = errno;
    if (::close(fd) != 0) return MediaProbe::Io; // unknown close stays counted
    handle.releaseAfterClose();
    if (!stated) return stat_error == EOVERFLOW ? MediaProbe::Io : MediaProbe::Missing;
    if (!S_ISREG(info.st_mode)) return MediaProbe::Missing;
    if (info.st_size < 0 || static_cast<uint64_t>(info.st_size) > INT32_MAX) return MediaProbe::Io;
    OperationLease still_current;
    if (!owner.acquireOperation(generation, still_current)) return MediaProbe::Stale;
    return MediaProbe::Present;
}
StorageFsBridge::StorageFsBridge() : fs::FS(fs::FSImplPtr(new (std::nothrow) BridgeImpl)) {}
bool StorageFsBridge::bind(uint64_t generation) {
    auto* impl=static_cast<BridgeImpl*>(_impl.get());
    if (!impl || !impl->binding || !impl->binding->usable()) return false;
    auto& b=*impl->binding;fs::FSLockGuard lock(b.mutex);
    if (b.status->handles.load()) return false;
    OperationLease operation;
    if (!StorageOwner::instance().acquireOperation(generation,operation)) { b.fail(FileError::StaleMedia);return false; }
    b.generation=generation;b.status->error.store(FileError::None);b.status->native_error.store(0);b.status->eof.store(false);
    return true;
}
std::shared_ptr<StorageFileStatus> StorageFsBridge::fileStatus() const {
    auto* impl=static_cast<BridgeImpl*>(_impl.get());
    return impl && impl->binding ? impl->binding->status : nullptr;
}
}}
