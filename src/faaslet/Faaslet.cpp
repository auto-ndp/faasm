#include <faaslet/Faaslet.h>

#include <conf/FaasmConfig.h>
#include <system/CGroup.h>
#include <system/NetworkNamespace.h>
#include <threads/ThreadState.h>
#include <wamr/WAMRWasmModule.h>
#include <wavm/WAVMWasmModule.h>

#include <faabric/scheduler/Scheduler.h>
#include <faabric/snapshot/SnapshotRegistry.h>
#include <faabric/util/config.h>
#include <faabric/util/environment.h>
#include <faabric/util/func.h>
#include <faabric/util/gids.h>
#include <faabric/util/locks.h>
#include <faabric/util/logging.h>
#include <faabric/util/timing.h>

#include <stdexcept>

#ifndef FAASM_SGX_DISABLED_MODE
#include <enclave/outside/EnclaveInterface.h>
#include <enclave/outside/system.h>
#endif
#include <storage/FileLoader.h>
#include <storage/FileSystem.h>

static thread_local bool threadIsIsolated = false;

using namespace isolation;

namespace faaslet {

void preloadPythonRuntime()
{
    conf::FaasmConfig& conf = conf::getFaasmConfig();
    if (conf.pythonPreload != "on") {
        SPDLOG_INFO("Not preloading python runtime");
        return;
    }

    SPDLOG_INFO("Preparing python runtime");

    faabric::Message msg =
      faabric::util::messageFactory(PYTHON_USER, PYTHON_FUNC);
    msg.set_ispython(true);
    msg.set_pythonuser("python");
    msg.set_pythonfunction("noop");
    faabric::util::setMessageId(msg);

    faabric::scheduler::Scheduler& sch = faabric::scheduler::getScheduler();
    sch.callFunction(msg, true);
}

Faaslet::Faaslet(faabric::MessageInBatch msg)
  : Executor(msg)
{
    conf::FaasmConfig& conf = conf::getFaasmConfig();

    // Instantiate the right wasm module for the chosen runtime
    if (conf.wasmVm == "sgx") {
#ifndef FAASM_SGX_DISABLED_MODE
        module = std::make_unique<wasm::EnclaveInterface>();
#else
        SPDLOG_ERROR(
          "SGX WASM VM selected, but SGX support disabled in config");
        throw std::runtime_error("SGX support disabled in config");
#endif
    } else if (conf.wasmVm == "wamr") {
        // Vanilla WAMR
        module = std::make_unique<wasm::WAMRWasmModule>(1);
    } else if (conf.wasmVm == "wavm") {
        module = std::make_unique<wasm::WAVMWasmModule>(1);
    } else {

        SPDLOG_ERROR("Unrecognised wasm VM: {}", conf.wasmVm);
        throw std::runtime_error("Unrecognised wasm VM");
    }
}

int32_t Faaslet::executeTask(int threadPoolIdx,
                             int msgIdx,
                             std::shared_ptr<faabric::BatchExecuteRequest> req)
{
    // Lazily setup Faaslet isolation.
    // This has to be done within the same thread as the execution (hence we
    // leave it until just before execution).
    // Because this is a thread-specific operation we don't need any
    // synchronisation here, and rely on the cgroup and network namespace
    // operations being thread-safe.

    if (!threadIsIsolated) {
        SPDLOG_INFO("Thread is not isolated");
        // Add this thread to the cgroup
        CGroup cgroup(BASE_CGROUP_NAME);
        cgroup.addCurrentThread();

        // Set up network namespace
        ns = claimNetworkNamespace();
        SPDLOG_INFO("Claimed network namespace");
        ns->addCurrentThread();

        threadIsIsolated = true;
    }   

    SPDLOG_INFO("Executing task");
    int32_t returnValue = module->executeTask(threadPoolIdx, msgIdx, req);
    SPDLOG_INFO("Finished executing task");
    return returnValue;
}

void Faaslet::reset(faabric::Message& msg)
{
    SPDLOG_INFO("[Faaslet.cpp] Resetting faaslet");
    if (module->isBound()) {
        module->reset(msg, localResetSnapshotKey);
    } else {
        conf::FaasmConfig& conf = conf::getFaasmConfig();
        module->bindToFunction(msg);
        // Create the reset snapshot for this function if it doesn't already
        // exist (currently only supported in WAVM)
        if (conf.wasmVm == "wavm") {
            // Create the reset snapshot for this function if it doesn't already
            // exist (currently only supported in WAVM)
            localResetSnapshotKey =
              wasm::getWAVMModuleCache().registerResetSnapshot(*module, msg);
        }
    }
}

void Faaslet::softShutdown()
{
    this->module.reset(nullptr);
}

void Faaslet::shutdown()
{
    if (ns != nullptr) {
        ns->removeCurrentThread();
        returnNetworkNamespace(ns);
    }

    Executor::shutdown();
}

std::span<uint8_t> Faaslet::getMemoryView()
{
    return module->getMemoryView();
}

void Faaslet::setMemorySize(size_t newSize)
{
    module->setMemorySize(newSize);
}

FaasletFactory::FaasletFactory()
{
    conf::FaasmConfig& conf = conf::getFaasmConfig();
    if (conf.wasmVm == "wavm") {
        wasm::WAVMWasmModule::warmUp();
    }
}
size_t Faaslet::getMaxMemorySize()
{
    return MAX_WASM_MEM;
}

std::string Faaslet::getLocalResetSnapshotKey()
{
    return localResetSnapshotKey;
}

FaasletFactory::~FaasletFactory() {}

std::shared_ptr<faabric::scheduler::Executor> FaasletFactory::createExecutor(
  faabric::MessageInBatch msg)
{
    return std::make_shared<Faaslet>(std::move(msg));
}

void FaasletFactory::flushHost()
{
    // Clear cached wasm and object files
    storage::FileLoader& fileLoader = storage::getFileLoader();
    fileLoader.clearLocalCache();

    // WAVM-specific flushing
    const conf::FaasmConfig& conf = conf::getFaasmConfig();
    if (conf.wasmVm == "wavm") {
        wasm::WAVMWasmModule::clearCaches();
    }
}
}
