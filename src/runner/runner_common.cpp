#include "runner_common.h"

#include <wavm/WAVMWasmModule.h>

void runner::commonInit()
{
    wasm::setupWavmHooks();
}
