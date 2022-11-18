
flatbuffers_generate_headers(
    TARGET cephclass_flat
    SCHEMAS "cephcomm.fbs"
)

# Ceph rados/objclass.h does not build in C++20 mode.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)
add_library(faasm_ceph SHARED cephclass.cpp)
target_link_libraries(faasm_ceph PRIVATE flatbuffers::flatbuffers cephclass_flat)
