# ----------------------------------------------
# Protobuf/gRPC generation
# ----------------------------------------------

set(PB_HEADER_COPIED "${FAABRIC_INCLUDE_DIR}/faabric/proto/faabric.pb.h")

add_library(proto STATIC faabric.proto)
target_link_libraries(proto PUBLIC protobuf::libprotobuf)
target_include_directories(proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS TARGET proto)

# ----------------------------------------------
# Faabric wrapper library
# ----------------------------------------------

target_link_libraries(faabric_common_dependencies INTERFACE proto)
add_library(faabric::proto ALIAS proto)
