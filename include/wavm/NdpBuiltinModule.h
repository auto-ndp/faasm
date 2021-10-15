#pragma once

#include <functional>
#include <string>
#include <vector>
#include <wasm/WasmModule.h>
#include <wasm/ndp.h>

namespace wasm {

class NDPBuiltinModule;

class NDPBuiltinModule final : public WasmModule
{
  public:
    NDPBuiltinModule();

    ~NDPBuiltinModule();

    // ----- Module lifecycle -----
    inline void reset(faabric::Message& msg, const std::string& snapshotKey) override { tearDown(); }

    int32_t executeFunction(faabric::Message& msg) override;

    inline int32_t executeOMPThread(int threadPoolIdx,
                                    uint32_t stackTop,
                                    faabric::Message& msg) override
    {
        return executeFunction(msg);
    }

    inline int32_t executePthread(int threadPoolIdx,
                                  uint32_t stackTop,
                                  faabric::Message& msg) override
    {
        return executeFunction(msg);
    }

    bool tearDown();

    inline void flush() override {}

    // ----- Memory management -----
    inline uint32_t growMemory(uint32_t nBytes) override { return 0; }

    inline uint32_t shrinkMemory(uint32_t nBytes) override { return 0; }

    inline uint32_t mmapMemory(uint32_t nBytes) override { return 0; }

    inline uint32_t mmapFile(uint32_t fp, uint32_t length) override
    {
        return 0;
    }

    inline void unmapMemory(uint32_t offset, uint32_t nBytes) override {}

    inline uint32_t mapSharedStateMemory(
      const std::shared_ptr<faabric::state::StateKeyValue>& kv,
      long offset,
      uint32_t length) override
    {
        return 0;
    }

    inline uint8_t* wasmPointerToNative(uint32_t wasmPtr) override
    {
        return nullptr;
    }

    inline size_t getMemorySizeBytes() override { return 0; }

    // ----- Environment variables
    inline void writeWasmEnvToMemory(uint32_t envPointers,
                                     uint32_t envBuffer) override
    {}

    // ----- Debug -----
    void printDebugInfo() override;

    // ----- Execution -----
    inline void writeArgvToMemory(uint32_t wasmArgvPointers,
                                  uint32_t wasmArgvBuffer) override
    {}

  protected:
    inline uint8_t* getMemoryBase() override { return nullptr; }

    // Module-specific binding
    inline void doBindToFunction(faabric::Message& msg, bool cache) override {}

  private:
    const BuiltinFunction* boundFn;
    std::vector<uint8_t> input, output;
};

}
