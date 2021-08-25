include_directories(
        ${FAASM_INCLUDE_DIR}/wavm
        ${FAASM_WAVM_SOURCE_DIR}/Lib
)

file(GLOB HEADERS "${FAASM_INCLUDE_DIR}/wavm/*.h")

set(LIB_FILES
        WAVMWasmModule.cpp
        NdpBuiltinModule.cpp
        WAVMModuleCache.cpp
        IRModuleCache.cpp
        LoadedDynamicModule.cpp
        syscalls.h
        chaining.cpp
        codegen.cpp
        dynlink.cpp
        env.cpp
        faasm.cpp
        io.cpp
        libcxx.cpp
        maths.cpp
        memory.cpp
        messages.cpp
        mpi.cpp
        ndp_objects.cpp
        network.cpp
        openmp.cpp
        process.cpp
        scheduling.cpp
        signals.cpp
        syscalls.cpp
        threads.cpp
        timing.cpp
        util.cpp
        ${HEADERS}
        )

faasm_private_lib(wavmmodule "${LIB_FILES}")
target_link_libraries(wavmmodule PUBLIC faabric wasm libWAVM threads)
