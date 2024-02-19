#include <absl/strings/str_split.h>
#include <boost/predef.h>
#include <conf/FaasmConfig.h>
#include <faabric/util/environment.h>
#include <faabric/util/logging.h>
#include <faabric/util/timing.h>
#include <mimalloc.h>

#include <algorithm>
#include <new>
#include <string_view>

using namespace faabric::util;
using namespace std::string_view_literals;

namespace conf {

CodegenTargetSpec determineNativeCodegenTarget()
{
    CodegenTargetSpec ts;
    const FaasmConfig& conf = getFaasmConfig();
#if (BOOST_ARCH_X86_64 == 1)
    ts.arch = "x86_64";
    ts.cpu = "skylake";
#elif (BOOST_ARCH_ARM >= BOOST_VERSION_NUMBER(8, 0, 0))
    ts.arch = "aarch64";
    ts.cpu = "cortex-a53";
#else
#error Unsupported CPU architecture
#endif
    const CodegenTargetSpec detectedTs = ts;
    for (const auto& cts : conf.codegenTargets) {
        if (cts.arch == detectedTs.arch) {
            ts.cpu = cts.cpu;
            break;
        }
    }
    SPDLOG_INFO("Using architecture {}:{}", ts.arch, ts.cpu);
    return ts;
}

CodegenTargetSpec nativeCodegenTarget()
{
    static CodegenTargetSpec ts = determineNativeCodegenTarget();
    return ts;
}

FaasmConfig& getFaasmConfig()
{
    static FaasmConfig conf;
    return conf;
}

FaasmConfig::FaasmConfig()
{
    this->initialise();
}

void FaasmConfig::initialise()
{
    hostType = getEnvVar("HOST_TYPE", "default");

    {
        auto vmam = getEnvVar("VM_ARENA_MODE", "default");
        if (vmam.empty() || vmam == "default") {
            vmArenaMode = VirtualMemoryArenaMode::Default;
        } else if (vmam == "uffd") {
            vmArenaMode = VirtualMemoryArenaMode::Uffd;
        } else {
            throw std::runtime_error("Invalid VM_ARENA_MODE value");
        }
    }
    cgroupMode = getEnvVar("CGROUP_MODE", "on");
    netNsMode = getEnvVar("NETNS_MODE", "off");
    maxNetNs = this->getIntParam("MAX_NET_NAMESPACES", "100");

    pythonPreload = getEnvVar("PYTHON_PRELOAD", "off");
    captureStdout = getEnvVar("CAPTURE_STDOUT", "off");

    wasmVm = getEnvVar("WASM_VM", "wavm");
    std::string rawCodegenTargets = getEnvVar("CODEGEN_TARGETS", "");
    codegenTargets.clear();
    if (!rawCodegenTargets.empty()) {
        for (std::string_view pairStr :
             absl::StrSplit(rawCodegenTargets, ';', absl::SkipEmpty())) {
            auto splitPos = pairStr.find_first_of(':');
            if (splitPos == std::string_view::npos) {
                throw std::runtime_error(
                  "Invalid format for CODEGEN_TARGETS, expected format: "
                  "arch:cpu;x86_64:skylake;aarch64:cortex-a53;aarch64:"
                  "thunderx2t99");
            }
            codegenTargets.emplace_back((CodegenTargetSpec){
              .arch = std::string(pairStr.substr(0, splitPos)),
              .cpu = std::string(pairStr.substr(splitPos + 1)) });
        }
    }
    chainedCallTimeout = this->getIntParam("CHAINED_CALL_TIMEOUT", "25000");

    std::string faasmLocalDir =
      getEnvVar("FAASM_LOCAL_DIR", "/usr/local/faasm");
    functionDir = fmt::format("{}/{}", faasmLocalDir, "wasm");
    objectFileDir = fmt::format("{}/{}", faasmLocalDir, "object");
    runtimeFilesDir = fmt::format("{}/{}", faasmLocalDir, "runtime_root");
    sharedFilesDir = fmt::format("{}/{}", faasmLocalDir, "shared");

    s3Bucket = getEnvVar("S3_BUCKET", "faasm");
    s3Host = getEnvVar("S3_HOST", "minio");
    s3Port = getEnvVar("S3_PORT", "9000");
    s3User = getEnvVar("S3_USER", "minio");
    s3Password = getEnvVar("S3_PASSWORD", "minio123");

    offload_cpu_threshold = stod(getEnvVar("OFFLOAD_CPU_THRESHOLD", "0.7"));
    offload_ram_threshold = stod(getEnvVar("OFFLOAD_RAM_THRESHOLD", "0.9"));
    offload_load_avg_threshold = stod(getEnvVar("OFFLOAD_LOAD_AVG_FACTOR_THRESHOLD", "0.75"));

    attestationProviderUrl = getEnvVar("AZ_ATTESTATION_PROVIDER_URL", "");
}

int FaasmConfig::getIntParam(const char* name, const char* defaultValue)
{
    int value = stoi(faabric::util::getEnvVar(name, defaultValue));

    return value;
}

void FaasmConfig::reset()
{
    this->initialise();
}

void FaasmConfig::print() const
{
    SPDLOG_INFO("--- HOST ---");
    SPDLOG_INFO("VM arena mode:        {}",
                vmArenaMode == VirtualMemoryArenaMode::Default ? "Default"
                                                               : "UFFD");
    SPDLOG_INFO("Cgroup mode:          {}", cgroupMode);
    SPDLOG_INFO("Host type:            {}", hostType);
    SPDLOG_INFO("Network ns mode:      {}", netNsMode);
    SPDLOG_INFO("Max. network ns:      {}", maxNetNs);

    SPDLOG_INFO("--- MISC ---");
    SPDLOG_INFO("Capture stdout:       {}", captureStdout);
    SPDLOG_INFO("Chained call timeout: {}", chainedCallTimeout);
    SPDLOG_INFO("Python preload:       {}", pythonPreload);
    SPDLOG_INFO("Wasm VM:              {}", wasmVm);
    if (codegenTargets.empty()) {
        SPDLOG_INFO("Codegen targets:      Native");
    } else {
        for (const auto& cgt : codegenTargets) {
            SPDLOG_INFO("Codegen target:       {}:{}", cgt.arch, cgt.cpu);
        }
    }
    SPDLOG_INFO("--- STORAGE ---");
    SPDLOG_INFO("Function dir:         {}", functionDir);
    SPDLOG_INFO("Object file dir:      {}", objectFileDir);
    SPDLOG_INFO("Runtime files dir:    {}", runtimeFilesDir);
    SPDLOG_INFO("Shared files dir:     {}", sharedFilesDir);

    SPDLOG_INFO("--- OFFLOADING THRESHOLDS ---");
    SPDLOG_INFO("CPU:                  {}", offload_cpu_threshold);
    SPDLOG_INFO("RAM:                  {}", offload_ram_threshold);
    SPDLOG_INFO("Load avg:             {}", offload_load_avg_threshold);
}
}

#define USE_TRACY_ALLOC_TRACING 0

inline void TraceAlloc(void* p, size_t n)
{
#if USE_TRACY_ALLOC_TRACING
    TracySecureAlloc(p, n);
#endif
}
inline void TraceFree(void* p)
{
#if USE_TRACY_ALLOC_TRACING
    TracySecureFree(p);
#endif
}

#if 1 && !(defined(__has_feature) && __has_feature(thread_sanitizer))

void operator delete(void* p) noexcept
{
    TraceFree(p);
    mi_free(p);
}
void operator delete[](void* p) noexcept
{
    TraceFree(p);
    mi_free(p);
}

void* operator new(std::size_t n) noexcept(false)
{
    void* p = mi_new(n);
    TraceAlloc(p, n);
    return p;
}
void* operator new[](std::size_t n) noexcept(false)
{
    void* p = mi_new(n);
    TraceAlloc(p, n);
    return p;
}

void* operator new(std::size_t n, const std::nothrow_t& tag) noexcept
{
    (void)(tag);
    void* p = mi_new_nothrow(n);
    TraceAlloc(p, n);
    return p;
}
void* operator new[](std::size_t n, const std::nothrow_t& tag) noexcept
{
    (void)(tag);
    void* p = mi_new_nothrow(n);
    TraceAlloc(p, n);
    return p;
}

// #if (__cplusplus >= 201402L || _MSC_VER >= 1916)
void operator delete(void* p, std::size_t n) noexcept
{
    TraceFree(p);
    mi_free_size(p, n);
}
void operator delete[](void* p, std::size_t n) noexcept
{
    TraceFree(p);
    mi_free_size(p, n);
}

// #if (__cplusplus > 201402L || defined(__cpp_aligned_new))
void operator delete(void* p, std::align_val_t al) noexcept
{
    TraceFree(p);
    mi_free_aligned(p, static_cast<size_t>(al));
}
void operator delete[](void* p, std::align_val_t al) noexcept
{
    TraceFree(p);
    mi_free_aligned(p, static_cast<size_t>(al));
}
void operator delete(void* p, std::size_t n, std::align_val_t al) noexcept
{
    TraceFree(p);
    mi_free_size_aligned(p, n, static_cast<size_t>(al));
}
void operator delete[](void* p, std::size_t n, std::align_val_t al) noexcept
{
    TraceFree(p);
    mi_free_size_aligned(p, n, static_cast<size_t>(al));
}

void* operator new(std::size_t n, std::align_val_t al) noexcept(false)
{
    void* p = mi_new_aligned(n, static_cast<size_t>(al));
    TraceAlloc(p, n);
    return p;
}
void* operator new[](std::size_t n, std::align_val_t al) noexcept(false)
{
    void* p = mi_new_aligned(n, static_cast<size_t>(al));
    TraceAlloc(p, n);
    return p;
}
void* operator new(std::size_t n,
                   std::align_val_t al,
                   const std::nothrow_t&) noexcept
{
    void* p = mi_new_aligned_nothrow(n, static_cast<size_t>(al));
    TraceAlloc(p, n);
    return p;
}
void* operator new[](std::size_t n,
                     std::align_val_t al,
                     const std::nothrow_t&) noexcept
{
    void* p = mi_new_aligned_nothrow(n, static_cast<size_t>(al));
    TraceAlloc(p, n);
    return p;
}

#endif
