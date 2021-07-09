#include <wavm/NdpBuiltinModule.h>

#include <algorithm>
#include <array>
#include <boost/filesystem.hpp>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/types.h>

#include <conf/FaasmConfig.h>
#include <faabric/util/bytes.h>
#include <faabric/util/config.h>
#include <faabric/util/files.h>
#include <faabric/util/func.h>
#include <faabric/util/locks.h>
#include <faabric/util/logging.h>
#include <faabric/util/memory.h>
#include <faabric/util/timing.h>
#include <wasm/WasmExecutionContext.h>
#include <wasm/ndp.h>

namespace fs = boost::filesystem;

namespace wasm {

static fs::path userObjectDirPath(const std::string& user)
{
    auto& conf = conf::getFaasmConfig();
    fs::path p(conf.sharedFilesStorageDir);
    p /= "objstore";
    p /= user;
    fs::create_directories(p);
    return p;
}

static std::string userObjectPath(const std::string& user,
                                  const std::vector<uint8_t>& key)
{
    std::string filename = faabric::util::bytesToString(key);
    return (userObjectDirPath(user) / filename).string();
}

static int ndpGet(NDPBuiltinModule& module, faabric::Message& msg)
{
    auto args = BuiltinNdpGetArgs::fromBytes(
      faabric::util::stringToBytes(msg.inputdata()));
    std::string objPath = userObjectPath(msg.user(), args.key);
    if (!fs::exists(objPath)) {
        SPDLOG_DEBUG("NDP GET file {} doesn't exist", objPath);
        return 1;
    }
    auto bytes = faabric::util::readFileToBytes(objPath);
    size_t startOffset = std::min(bytes.size(), args.offset);
    size_t dataLen = std::min(bytes.size() - startOffset, args.uptoBytes);
    SPDLOG_INFO("NDP GET {} bytes starting at offset {} from file {} "
                "[bytes={}, args.offset={}, uptoBytes={}]",
                dataLen,
                startOffset,
                objPath,
                bytes.size(),
                args.offset,
                args.uptoBytes);
    msg.set_outputdata(static_cast<void*>(bytes.data() + startOffset),
                       startOffset + dataLen);
    return 0;
}

static int ndpPut(NDPBuiltinModule& module, faabric::Message& msg)
{
    auto args = BuiltinNdpPutArgs::fromBytes(
      faabric::util::stringToBytes(msg.inputdata()));
    std::string objPath = userObjectPath(msg.user(), args.key);
    SPDLOG_INFO("NDP PUT {} bytes to file {}", args.value.size(), objPath);
    faabric::util::writeBytesToFile(objPath, args.value);
    return 0;
}

const std::array<BuiltinFunction, 2> NDP_BUILTINS{
    { { BUILTIN_NDP_GET_FUNCTION, &ndpGet },
      { BUILTIN_NDP_PUT_FUNCTION, &ndpPut } }
};

NDPBuiltinModule::NDPBuiltinModule()
  : boundFn(nullptr)
  , input()
  , output()
{}

NDPBuiltinModule::~NDPBuiltinModule()
{
    tearDown();
}

bool NDPBuiltinModule::tearDown()
{
    this->input.clear();
    this->output.clear();
    return true;
}

/**
 * Executes the given function call
 */
int32_t NDPBuiltinModule::executeFunction(faabric::Message& msg)
{
    SPDLOG_INFO(
      "executing NDP builtin `{}` (user {})", msg.function(), msg.user());

    WasmExecutionContext _wec(this, &msg);
    boundFn = &getNdpBuiltin(msg.function());

    // Call the function
    int returnValue = 0;
    SPDLOG_DEBUG("Builtin execute {}", boundFunction);
    try {
        if (this->boundFn == nullptr) {
            throw std::runtime_error(
              "Builtin bound function definition is null");
        }
        if (this->boundFn->function == nullptr) {
            throw std::runtime_error("Builtin bound function pointer is null");
        }
        returnValue = this->boundFn->function(*this, msg);
    } catch (...) {
        SPDLOG_ERROR("Exception caught executing a builtin");
        returnValue = 1;
    }

    // Record the return value
    msg.set_returnvalue(returnValue);

    return returnValue;
}

void NDPBuiltinModule::printDebugInfo()
{
    printf("\n------ Builtin Module debug info ------\n");

    if (isBound()) {
        printf("Bound user:         %s\n", boundUser.c_str());
        printf("Bound function:     %s\n", boundFunction.c_str());
        printf("Input length:       %lu\n", (unsigned long)input.size());
        printf("Output length:      %lu\n", (unsigned long)output.size());

        filesystem.printDebugInfo();
    } else {
        printf("Unbound\n");
    }

    printf("-------------------------------\n");

    fflush(stdout);
}

}
