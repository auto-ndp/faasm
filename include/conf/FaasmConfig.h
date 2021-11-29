#pragma once

#include <string>
#include <vector>

namespace conf {

struct CodegenTargetSpec {
    std::string arch;
    std::string cpu;
};

CodegenTargetSpec nativeCodegenTarget();

enum class VirtualMemoryArenaMode {
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

    FaasmConfig();

    void reset();

    void print();

  private:
    int getIntParam(const char* name, const char* defaultValue);

    void initialise();
};

FaasmConfig& getFaasmConfig();
}
