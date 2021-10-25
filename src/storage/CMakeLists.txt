faasm_private_lib(storage
    FileDescriptor.cpp
    FileLoader.cpp
    FileSystem.cpp
    S3Wrapper.cpp
    SharedFiles.cpp
)
add_dependencies(storage aws_ext)
target_include_directories(storage PRIVATE ${FAASM_INCLUDE_DIR}/storage)

target_link_libraries(storage PUBLIC
    faasm::wavmmodule
    faasm::wamrmodule
    aws_ext_core
    aws_ext_s3
    cpprestsdk::cpprestsdk
)
