#include "WAVMWasmModule.h"

#include <WAVM/IR/Module.h>
#include <WAVM/IR/Types.h>
#include <WAVM/Runtime/Runtime.h>
#include <WAVM/WASM/WASM.h>
#include <WAVM/WASTParse/WASTParse.h>

#include <conf/FaasmConfig.h>
#include <faabric/util/files.h>
#include <faabric/util/logging.h>

using namespace WAVM;

namespace wasm {
std::vector<uint8_t> wavmCodegen(std::vector<uint8_t>& bytes,
                                 conf::CodegenTargetSpec target,
                                 const std::string& fileName)
{

    IR::Module moduleIR;

    // Feature flags
    moduleIR.featureSpec.simd = true;

    if (faabric::util::isWasm(bytes)) {
        // Handle WASM
        WASM::LoadError loadError;
        bool success = WASM::loadBinaryModule(
          bytes.data(), bytes.size(), moduleIR, &loadError);
        if (!success) {
            SPDLOG_ERROR("Failed to parse wasm binary at {}", fileName);
            SPDLOG_ERROR("Parse failure: {}", loadError.message);

            throw std::runtime_error("Failed to parse wasm binary");
        }
    } else {
        std::vector<WAST::Error> parseErrors;
        bool success = WAST::parseModule(
          (const char*)bytes.data(), bytes.size(), moduleIR, parseErrors);

        SPDLOG_ERROR("Failed to parse non-wasm binary as wast: {}", fileName);

        WAST::reportParseErrors(
          "wast_file", (const char*)bytes.data(), parseErrors);

        if (!success) {
            throw std::runtime_error("Failed to parse non-wasm file");
        }
    }

    // Compile the module to object code
    return Runtime::precompileModule(moduleIR, target.arch, target.cpu);
}
}
