#include <faabric/util/crash.h>
#include <faabric/util/files.h>
#include <faabric/util/logging.h>
#include <faabric/util/macros.h>

#include <array>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/mman.h>
#include <sys/signal.h>
#include <unistd.h>

#include <absl/debugging/stacktrace.h>
#include <absl/debugging/symbolize.h>

const std::string_view ABORT_MSG = "Caught stack backtrace:\n";
constexpr int TEST_SIGNAL = 12341234;

// Must be async-signal-safe - don't call allocating functions
void crashHandler(int sig, siginfo_t* siginfo, void* contextR) noexcept
{
    ucontext_t* context = static_cast<ucontext_t*>(contextR);
    UNUSED(context);
    UNUSED(siginfo);
    faabric::util::handleCrash(sig);
}

namespace faabric::util {

void handleCrash(int sig)
{
    std::array<void*, 32> stackPtrs;
    size_t filledStacks = backtrace(stackPtrs.data(), stackPtrs.size());
    if (sig != TEST_SIGNAL) {
        write(STDERR_FILENO, ABORT_MSG.data(), ABORT_MSG.size());
    }
    faabric::util::printStackTrace();
    if (sig != TEST_SIGNAL) {
        signal(sig, SIG_DFL);
        raise(sig);
        exit(1);
    }
}

void printStackTrace(void* contextR)
{
    constexpr size_t NUM_STACKS = 32;
    std::array<void*, NUM_STACKS> stackPtrs;
    std::array<char, 1024> symbolName;
    int droppedFrames = 0;
    size_t filledStacks = absl::GetStackTraceWithContext(
      stackPtrs.data(), stackPtrs.size(), 0, contextR, &droppedFrames);

    for (int entry = 0; entry < filledStacks; entry++) {
        bool hasSymbol = absl::Symbolize(
          stackPtrs[entry], symbolName.data(), symbolName.size());
        fprintf(stderr,
                "[%2d] 0x%016zx: %s\n",
                entry,
                (size_t)(stackPtrs[entry]),
                hasSymbol ? symbolName.data() : "<unknown symbol>");
    }
}

void setUpCrashHandler(int sig)
{
    // cmdline is null-separated, so argv[0] == argv0
    std::string argv0 = faabric::util::readFileToString("/proc/self/cmdline");
    absl::InitializeSymbolizer(argv0.data());
    std::vector<int> sigs;
    if (sig >= 0) {
        sigs = { sig };
    } else {
        fputs("Testing crash handler backtrace:\n", stderr);
        fflush(stderr);
        siginfo_t dummy_siginfo = {};
        crashHandler(TEST_SIGNAL, &dummy_siginfo, nullptr);
        SPDLOG_INFO("Installing crash handler");

        // We don't handle SIGSEGV here because segfault handling is
        // necessary for dirty tracking and if this handler gets initialised
        // after the one for dirty tracking it thinks legitimate dirty tracking
        // segfaults are crashes
        sigs = { SIGSEGV, SIGABRT, SIGILL, SIGFPE };
    }
    constexpr size_t sigstack_size = 16384;
    void* sigstack = mmap(nullptr,
                          sigstack_size,
                          PROT_READ | PROT_WRITE,
                          MAP_ANONYMOUS | MAP_PRIVATE,
                          -1,
                          0);
    if (sigstack == MAP_FAILED || sigstack == nullptr) {
        throw std::runtime_error(
          "Couldn't allocate memory for signal alternate stack");
    }
    stack_t sigstk = {};
    sigstk.ss_flags = 0;
    sigstk.ss_size = sigstack_size;
    sigstk.ss_sp = sigstack;
    if (sigaltstack(&sigstk, nullptr) < 0) {
        throw std::runtime_error("Couldn't install signal alternate stack");
    }

    struct sigaction action;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    action.sa_handler = nullptr;
    action.sa_sigaction = &crashHandler;
    sigemptyset(&action.sa_mask);
    for (auto signo : sigs) {
        if (sigaction(signo, &action, nullptr) < 0) {
            SPDLOG_WARN("Couldn't install handler for signal {}", signo);
        } else {
            SPDLOG_INFO("Installed handler for signal {}", signo);
        }
    }
}

}
