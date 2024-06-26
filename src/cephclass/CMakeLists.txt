
flatbuffers_generate_headers(
    TARGET cephclass_flat
    SCHEMAS "cephcomm.fbs"
)

find_package(spdlog REQUIRED)

# Ceph rados/objclass.h does not build in C++20 mode.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)

add_library(faasm_ceph_comm INTERFACE)
target_include_directories(faasm_ceph_comm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(faasm_ceph_comm INTERFACE flatbuffers::flatbuffers cephclass_flat)
add_library(faasm::ceph_comm ALIAS faasm_ceph_comm)

add_library(faasm_ceph SHARED cephclass.cpp)
target_link_libraries(faasm_ceph PRIVATE faasm::ceph_comm fmt::fmt spdlog::spdlog)
target_compile_definitions(faasm_ceph INTERFACE
    FMT_DEPRECATED= # Suppress warnings about use of deprecated api by spdlog
)
