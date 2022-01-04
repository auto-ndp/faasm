#pragma once

#include <faabric/scheduler/ExecutorFactory.h>
#include <faabric/scheduler/Scheduler.h>
#include <faabric/util/func.h>

#include <system/NetworkNamespace.h>
#include <wasm/WasmModule.h>

#include <string>

namespace faaslet {

class Faaslet final : public faabric::scheduler::Executor
{
  public:
    explicit Faaslet(faabric::Message& msg);

    std::unique_ptr<wasm::WasmModule> module;

    void reset(faabric::Message& msg) override;

    int32_t executeTask(
      int threadPoolIdx,
      int msgIdx,
      std::shared_ptr<faabric::BatchExecuteRequest> req) override;

    faabric::util::MemoryView getMemoryView() override;

    void restore(faabric::Message& call) override;

    std::string getLocalResetSnapshotKey();

  protected:
    void postFinish() override;

    void softShutdown() override;

  private:
    std::string localResetSnapshotKey;

    std::shared_ptr<isolation::NetworkNamespace> ns;
};

class FaasletFactory final : public faabric::scheduler::ExecutorFactory
{
  public:
    FaasletFactory();
    ~FaasletFactory();

  protected:
    std::shared_ptr<faabric::scheduler::Executor> createExecutor(
      faabric::Message& msg) override;

    void flushHost() override;
};

void preloadPythonRuntime();
}
