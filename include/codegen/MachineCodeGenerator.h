#pragma once

#include <conf/FaasmConfig.h>
#include <storage/FileLoader.h>

#include <cstdint>
#include <span>

namespace codegen {

class MachineCodeGenerator
{
  public:
    MachineCodeGenerator();

    MachineCodeGenerator(storage::FileLoader& loaderIn);

    void codegenForFunction(faabric::Message& msg, bool clean = false);

    void codegenForSharedObject(const std::string& inputPath);

    static std::vector<uint8_t> hashBytes(std::span<const uint8_t> bytes);

  private:
    conf::FaasmConfig& conf;
    storage::FileLoader& loader;

    std::vector<uint8_t> doCodegen(std::vector<uint8_t>& bytes,
                                   conf::CodegenTargetSpec target,
                                   const std::string& fileName,
                                   bool isSgx = false);
};

MachineCodeGenerator& getMachineCodeGenerator();
}
