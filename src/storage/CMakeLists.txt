include_directories(
        ${FAASM_INCLUDE_DIR}/storage
        ${FAASM_WAVM_SOURCE_DIR}/Lib
)

file(GLOB HEADERS "${FAASM_INCLUDE_DIR}/storage/*.h")

set(LIB_FILES
        FileDescriptor.cpp
        FileLoader.cpp
        FileSystem.cpp
        S3Wrapper.cpp
        SharedFiles.cpp
        ${HEADERS}
        )

faasm_private_lib(storage "${LIB_FILES}")
add_dependencies(storage aws_ext)

target_link_libraries(storage PUBLIC
    faabric
    wavmmodule
    wamrmodule
    aws_ext_core
    aws_ext_s3
    cpprestsdk::cpprest
    curl
)
