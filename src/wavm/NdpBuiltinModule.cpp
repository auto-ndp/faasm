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
    const auto& inputData = msg.inputdata();
    auto args = BuiltinNdpGetArgs::fromBytes(
      std::span(BYTES_CONST(inputData.data()), inputData.size()));
    TracyMessageL("Parsed input");
    std::string objPath = userObjectPath(msg.user(), args.key);
    if (!fs::exists(objPath)) {
        SPDLOG_DEBUG("NDP GET file {} doesn't exist", objPath);
        return 1;
    }
    TracyMessageL("Checked file existance");

    int fd = open(objPath.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("Couldn't open file " + objPath);
    }
    struct stat statbuf;
    int staterr = fstat(fd, &statbuf);
    if (staterr < 0) {
        throw std::runtime_error("Couldn't stat file " + objPath);
    }
    size_t fsize = statbuf.st_size;
    args.offset = std::min(fsize, args.offset);
    args.uptoBytes = std::min(args.uptoBytes, fsize - args.offset);
    posix_fadvise(fd, args.offset, args.uptoBytes, POSIX_FADV_SEQUENTIAL);
    std::string* outdata = msg.mutable_outputdata();
    outdata->resize(fsize, '\0');
    lseek(fd, args.offset, SEEK_SET);
    int cpos = 0;
    while (cpos < args.uptoBytes) {
        int rc = read(fd, outdata->data() + cpos, args.uptoBytes - cpos);
        if (rc < 0) {
            perror("Couldn't read file");
            throw std::runtime_error("Couldn't read file " + objPath);
        } else {
            cpos += rc;
        }
    }
    close(fd);

    TracyMessageL("Read file to bytes");
    SPDLOG_INFO("NDP GET from file {} "
                "[bytes={}, args.offset={}, uptoBytes={}]",
                objPath,
                msg.outputdata().size(),
                args.offset,
                args.uptoBytes);
    return 0;
}

static int ndpPut(NDPBuiltinModule& module, faabric::Message& msg)
{
    ZoneScopedN("NdpBuiltinModule::ndpPut");
    const auto& inputData = msg.inputdata();
    auto args = BuiltinNdpPutArgs::fromBytes(
      std::span(BYTES_CONST(inputData.data()), inputData.size()));
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
