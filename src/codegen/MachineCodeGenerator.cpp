#include <codegen/MachineCodeGenerator.h>
#include <storage/FileLoader.h>
#include <wamr/WAMRWasmModule.h>
#include <wavm/WAVMWasmModule.h>

#include <openssl/sha.h>
#include <stdexcept>

#include <faabric/util/bytes.h>
#include <faabric/util/config.h>
#include <faabric/util/files.h>
#include <faabric/util/func.h>

using namespace faabric::util;

namespace codegen {

MachineCodeGenerator& getMachineCodeGenerator()
{
    static thread_local MachineCodeGenerator gen;
    return gen;
}

MachineCodeGenerator::MachineCodeGenerator()
  : conf(conf::getFaasmConfig())
  , loader(storage::getFileLoader())
{}

MachineCodeGenerator::MachineCodeGenerator(storage::FileLoader& loaderIn)
  : conf(conf::getFaasmConfig())
  , loader(loaderIn)
{}

std::vector<uint8_t> MachineCodeGenerator::hashBytes(
  std::span<const uint8_t> bytes)
{
    std::vector<uint8_t> result(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),
           bytes.size(),
           result.data());

    return result;
}

std::vector<uint8_t> MachineCodeGenerator::doCodegen(
  std::vector<uint8_t>& bytes,
  conf::CodegenTargetSpec target,
  const std::string& fileName,
  bool isSgx)
{
    if (conf.wasmVm == "wamr") {
        return wasm::wamrCodegen(bytes, target, false);
    } else if (conf.wasmVm == "sgx") {
        return wasm::wamrCodegen(bytes, target, true);
    } else {
        return wasm::wavmCodegen(bytes, target, fileName);
    }
}

void MachineCodeGenerator::codegenForFunction(faabric::Message& msg)
{
    std::vector<uint8_t> bytes = loader.loadFunctionWasm(msg);

    const std::string funcStr = funcToString(msg, false);

    if (bytes.empty()) {
        throw std::runtime_error("Loaded empty bytes for " + funcStr);
    }

    for (const auto& target : conf.codegenTargets) {
        // Compare hashes
        std::vector<uint8_t> newHash = hashBytes(bytes);
        std::vector<uint8_t> oldHash;
        if (conf.wasmVm == "wamr" || conf.wasmVm == "sgx") {
            oldHash = loader.loadFunctionWamrAotHash(msg, target);
        } else if (conf.wasmVm == "wavm" && msg.issgx()) {
            oldHash = loader.loadFunctionObjectHash(msg);
        } else {
            SPDLOG_ERROR("Unrecognised WASM VM during codegen: {}", conf.wasmVm);
        	throw std::runtime_error("Unrecognised WASM VM");
        }

        if ((!oldHash.empty()) && newHash == oldHash) {
            // Even if we skip the code generation step, we want to sync the
            // latest object file
            if (conf.wasmVm == "wamr") {
                UNUSED(loader.loadFunctionWamrAotHash(msg, target));
            } else {
                UNUSED(loader.loadFunctionObjectFile(msg, target));
            }
	        SPDLOG_DEBUG(
	          "Skipping codegen for {} (WASM VM: {})", funcStr, conf.wasmVm);
            return;
        } else if (oldHash.empty()) {
	        SPDLOG_DEBUG(
	          "No old hash found for {} (WASM VM: {})", funcStr, conf.wasmVm);
        } else {
	        SPDLOG_DEBUG(
	          "Hashes differ for {} (WASM VM: {})", funcStr, conf.wasmVm);
        }

        // Run the actual codegen
        std::vector<uint8_t> objBytes;
        try {
            objBytes = doCodegen(bytes, target, funcStr);
        } catch (std::runtime_error& ex) {
	        SPDLOG_ERROR(
	          "Codegen failed for {} (WASM VM: {})", funcStr, conf.wasmVm);
            throw ex;
        }

        // Upload the file contents and the hash
    	if (conf.wasmVm == "wamr" || conf.wasmVm == "sgx") {
            loader.uploadFunctionWamrAotFile(msg, target, objBytes);
            loader.uploadFunctionWamrAotHash(msg, target, newHash);
        } else {
            loader.uploadFunctionObjectFile(msg, target, objBytes);
            loader.uploadFunctionObjectHash(msg, target, newHash);
        }
    }
}

void MachineCodeGenerator::codegenForSharedObject(const std::string& inputPath)
{
    // Load the wasm
    std::vector<uint8_t> bytes = loader.loadSharedObjectWasm(inputPath);

    // Check the hash
    std::vector<uint8_t> newHash = hashBytes(bytes);
    for (const auto& target : conf.codegenTargets) {
        std::vector<uint8_t> oldHash =
          loader.loadSharedObjectObjectHash(inputPath, target);

        if ((!oldHash.empty()) && newHash == oldHash) {
            // Even if we skip the code generation step, we want to sync the
            // latest shared object object file
            UNUSED(loader.loadSharedObjectObjectFile(inputPath, target));
            SPDLOG_DEBUG("Skipping codegen for {}", inputPath);
            return;
        }

        // Run the actual codegen
        std::vector<uint8_t> objBytes = doCodegen(bytes, target, inputPath);

        // Do the upload
    	if (conf.wasmVm == "wamr" || conf.wasmVm == "sgx") {
            throw std::runtime_error(
              "Codegen for shared objects not supported with WAMR");
        }

        loader.uploadSharedObjectObjectFile(inputPath, target, objBytes);
        loader.uploadSharedObjectObjectHash(inputPath, target, newHash);
    }
}
}
