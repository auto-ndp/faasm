#pragma once

#include <string>
#include <vector>

#define ONE_MB_BYTES 1024 * 1024

#define WASM_BYTES_PER_PAGE 65536

// Note: this is *not* controlling the size provisioned by the linker, that is
// hard-coded in the build. This variable is just here for reference and must be
// updated to match the value in the build.
#define STACK_SIZE (4 * ONE_MB_BYTES)
#define THREAD_STACK_SIZE (2 * ONE_MB_BYTES)

// Properties of dynamic modules. Heap size must be wasm-module-page-aligned.
// One page is 64kB
#define DYNAMIC_MODULE_STACK_SIZE (2 * ONE_MB_BYTES)
#define DYNAMIC_MODULE_MEMORY_SIZE (66 * WASM_BYTES_PER_PAGE)
#define GUARD_REGION_SIZE (10 * WASM_BYTES_PER_PAGE)

namespace conf {

struct CodegenTargetSpec
{
    std::string arch;
    std::string cpu;
};

CodegenTargetSpec nativeCodegenTarget();

enum class VirtualMemoryArenaMode
{
    Default,
    Uffd
};

class FaasmConfig
{
  public:
    std::string hostType;

    VirtualMemoryArenaMode vmArenaMode;
    std::string cgroupMode;
    std::string netNsMode;
    int maxNetNs;

    std::string pythonPreload;
    std::string captureStdout;

    int chainedCallTimeout;

    std::string wasmVm;

    // arch:cpu;arch:cpu;arch:cpu...
    std::vector<CodegenTargetSpec> codegenTargets;

    std::string functionDir;
    std::string objectFileDir;
    std::string runtimeFilesDir;
    std::string sharedFilesDir;

    std::string s3Bucket;
    std::string s3Host;
    std::string s3Port;
    std::string s3User;
    std::string s3Password;

    std::string attestationProviderUrl;

    FaasmConfig();

    void reset();

    void print() const;

  private:
    int getIntParam(const char* name, const char* defaultValue);

    void initialise();
};

FaasmConfig& getFaasmConfig();
}
