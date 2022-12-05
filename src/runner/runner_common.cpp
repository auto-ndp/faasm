#include "runner_common.h"

#include <wavm/WAVMWasmModule.h>

namespace runner {

void commonInit()
{
    wasm::setupWavmHooks();
}

}
