#include "WAVMWasmModule.h"
#include "syscalls.h"

#include <linux/membarrier.h>

#include <WAVM/Runtime/Intrinsics.h>
#include <WAVM/Runtime/Runtime.h>
#include <faabric/util/bytes.h>
#include <faabric/util/config.h>
#include <faabric/util/logging.h>
#include <faabric/util/timing.h>

using namespace WAVM;

namespace wasm {

I32 s__madvise(I32 address, I32 numBytes, I32 advice)
{
    SPDLOG_DEBUG("S - madvise - {} {} {}", address, numBytes, advice);

    return 0;
}

I32 s__membarrier(I32 a)
{
    SPDLOG_DEBUG("S - membarrier - {}", a);

    int res;
    if (a == MEMBARRIER_CMD_QUERY) {
        res = syscall(__NR_membarrier, MEMBARRIER_CMD_QUERY, 0);
    } else if (a == MEMBARRIER_CMD_SHARED ||
               a == MEMBARRIER_CMD_PRIVATE_EXPEDITED ||
               a == MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED) {
        // We can ignore all non-query membarrier operations
        res = 0;
    } else {
        SPDLOG_ERROR("Unexpected membarrier argument {}", a);
        throw std::runtime_error("Invalid membarrier command");
    }

    return res;
}

std::shared_ptr<faabric::state::StateKeyValue> getStateKV(I32 keyPtr,
                                                          size_t size)
{
    const std::pair<std::string, std::string> userKey =
      getUserKeyPairFromWasm(keyPtr);
    faabric::state::State& s = faabric::state::getGlobalState();
    auto kv = s.getKV(userKey.first, userKey.second, size);

    return kv;
}

std::shared_ptr<faabric::state::StateKeyValue> getStateKV(I32 keyPtr)
{
    const std::pair<std::string, std::string> userKey =
      getUserKeyPairFromWasm(keyPtr);
    faabric::state::State& s = faabric::state::getGlobalState();
    auto kv = s.getKV(userKey.first, userKey.second);

    return kv;
}

I32 doMmap(I32 addr, I32 length, I32 prot, I32 flags, I32 fd, I32 offset)
{

    SPDLOG_DEBUG(
      "S - mmap - {} {} {} {} {} {}", addr, length, prot, flags, fd, offset);

    // Although we are ignoring the offset we should probably
    // double check when something explicitly requests one
    if (offset != 0) {
        SPDLOG_WARN("WARNING: ignoring non-zero mmap offset ({})", offset);
    }

    // Likewise with the address hint
    if (addr != 0) {
        SPDLOG_WARN("WARNING: ignoring mmap hint at {}", addr);
    }

    WAVMWasmModule* module = getExecutingWAVMModule();

    if (fd != -1) {
        // If fd is provided, we're mapping a file into memory
        storage::FileDescriptor& fileDesc =
          module->getFileSystem().getFileDescriptor(fd);
        return module->mmapFile(fileDesc.getLinuxFd(), length);
    } else {
        // Map memory
        return module->mmapMemory(length);
    }
}

/**
 * Note that syscall 192 is mmap2, which has the same interface as mmap except
 * that the final argument specifies the offset into the file in 4096-byte units
 * (instead of bytes, as is done by mmap). Given that we ignore the offset we
 * can just treat it like mmap
 */
I32 s__mmap(I32 addr, I32 length, I32 prot, I32 flags, I32 fd, I32 offset)
{
    return doMmap(addr, length, prot, flags, fd, offset);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "mmap",
                               I32,
                               wasi_mmap,
                               I32 addr,
                               I32 length,
                               I32 prot,
                               I32 flags,
                               I32 fd,
                               I64 offset)
{
    return doMmap(addr, length, prot, flags, fd, (I32)offset);
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "munmap",
                               I32,
                               wasi_munmap,
                               I32 addr,
                               I32 length)
{

    SPDLOG_DEBUG("S - munmap - {} {}", addr, length);

    WasmModule* executingModule = getExecutingWAVMModule();
    executingModule->unmapMemory(addr, length);

    return 0;
}

/**
 * Note that sbrk should only be called indirectly through musl. The required
 * behaviour is:
 *
 * - brk(0) returns the current break
 * - returns the new break if successful
 * - returns -1 if there's an issue and sets errno
 *
 * Note that we assume the address is page-aligned and shrink memory if
 * necessary.
 */
WAVM_DEFINE_INTRINSIC_FUNCTION(env, "__sbrk", I32, __sbrk, I32 increment)
{
    SPDLOG_TRACE("S - sbrk - {}", increment);

    WAVMWasmModule* module = getExecutingWAVMModule();
    I32 result;
    if (increment < 0) {
        PROF_START(sbrkShrink)
        result = module->shrinkMemory(-increment);
        PROF_END(sbrkShrink)
    } else {
        PROF_START(sbrkGrow)
        result = module->growMemory(increment);
        PROF_END(sbrkGrow)
    }

    return result;
}

// mprotect is usually called as part of thread creation, in which
// case we can ignore it.
I32 s__mprotect(I32 addrPtr, I32 len, I32 prot)
{
    SPDLOG_DEBUG("S - mprotect - {} {} {}", addrPtr, len, prot);

    return 0;
}

WAVM_DEFINE_INTRINSIC_FUNCTION(env,
                               "shm_open",
                               I32,
                               shm_open,
                               I32 a,
                               I32 b,
                               I32 c)
{
    SPDLOG_DEBUG("S - shm_open - {} {} {}", a, b, c);
    throwException(Runtime::ExceptionTypes::calledUnimplementedIntrinsic);
}

void memoryLink() {}
}
