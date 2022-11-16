faasm_private_lib(storage_rados
    S3Wrapper.cpp
)
target_include_directories(storage_rados PRIVATE ${FAASM_INCLUDE_DIR}/storage)
target_link_libraries(storage_rados PUBLIC
    faasm::common_deps
)

faasm_private_lib(storage
    FileDescriptor.cpp
    FileLoader.cpp
    FileSystem.cpp
    SharedFiles.cpp
)
target_include_directories(storage PRIVATE ${FAASM_INCLUDE_DIR}/storage)
target_link_libraries(storage PUBLIC
    faasm::storage_rados
    faasm::wavmmodule
    faasm::wamrmodule
    cpprestsdk::cpprestsdk
)
