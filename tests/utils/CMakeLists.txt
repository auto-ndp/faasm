include_directories(${CMAKE_CURRENT_LIST_DIR})

set(LIB_FILES
    system_utils.cpp
    worker_utils.cpp
    utils.h
)

faasm_private_lib(test_utils "${LIB_FILES}")
add_library(faasm::test_utils ALIAS test_utils)

target_link_libraries(test_utils PUBLIC
    codegen
    upload_lib
    runner_lib
    cpprestsdk::cpprestsdk
    faaslet_lib
    wavmmodule
    wamrmodule
    system
    faasm::sgx
    faabric
    Catch2::Catch2
    faabric::test_utils
    faabric::faabric
)
