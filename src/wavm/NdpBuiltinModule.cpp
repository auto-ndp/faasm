#include <wavm/NdpBuiltinModule.h>

#include <algorithm>
#include <array>
#include <boost/filesystem.hpp>
#include <stdexcept>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <conf/FaasmConfig.h>
#include <faabric/util/bytes.h>
#include <faabric/util/config.h>
#include <faabric/util/files.h>
#include <faabric/util/func.h>
#include <faabric/util/locks.h>
#include <faabric/util/logging.h>
#include <faabric/util/memory.h>
#include <faabric/util/timing.h>
#include <storage/S3Wrapper.h>
#include <wasm/WasmExecutionContext.h>
#include <wasm/ndp.h>

namespace fs = boost::filesystem;

namespace wasm {

static fs::path userObjectDirPath(const std::string& user)
{
    auto& conf = conf::getFaasmConfig();
    fs::path p(conf.sharedFilesDir);
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
    ZoneScopedN("NdpBuiltinModule::ndpGet");
    static storage::S3Wrapper s3w;
    const auto& inputData = msg.inputdata();
    auto args = BuiltinNdpGetArgs::fromBytes(
      std::span(BYTES_CONST(inputData.data()), inputData.size()));
    const std::string key = faabric::util::bytesToString(args.key);

    std::string* outdata = msg.mutable_outputdata();

    try {
        s3w.getKeyPartIntoStr(
          msg.user(), key, args.offset, args.uptoBytes, *outdata);
    } catch (const std::runtime_error& err) {
        SPDLOG_WARN("NDP GET file {}/{} couldn't be read", msg.user(), key);
        return 1;
    }

    SPDLOG_DEBUG("NDP GET from file {}/{} "
                 "[bytes={}, args.offset={}, uptoBytes={}]",
                 msg.user(),
                 key,
                 msg.outputdata().size(),
                 args.offset,
                 args.uptoBytes);
    return 0;
}

static int ndpPut(NDPBuiltinModule& module, faabric::Message& msg)
{
    ZoneScopedN("NdpBuiltinModule::ndpPut");
    static storage::S3Wrapper s3w;
    const auto& inputData = msg.inputdata();
    const auto args = BuiltinNdpPutArgs::fromBytes(
      std::span(BYTES_CONST(inputData.data()), inputData.size()));
    const std::string key = faabric::util::bytesToString(args.key);
    SPDLOG_DEBUG(
      "NDP PUT {} bytes to {}/{}", args.value.size(), msg.user(), key);
    s3w.addKeyBytes(msg.user(), key, args.value);
    return 0;
}

const std::array<BuiltinFunction, 2> NDP_BUILTINS{
    { { BUILTIN_NDP_GET_FUNCTION, &ndpGet },
      { BUILTIN_NDP_PUT_FUNCTION, &ndpPut } }
};

NDPBuiltinModule::NDPBuiltinModule()
  : boundFn(nullptr)
{
}

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
