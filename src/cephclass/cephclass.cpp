#include <flatbuffers/buffer.h>
#include <rados/buffer_fwd.h>
#define FMT_ENFORCE_COMPILE_STRING

#include <cstdint>
#include <cstdio>
#include <exception>
#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/verifier.h>
#include <fmt/compile.h>
#include <fmt/core.h>
#include <memory>

#include <rados/objclass.h>
#include <stdexcept>

#include "cephclass/cephcomm.h"
#include "cephcomm_generated.h"

CLS_VER(1, 0)
CLS_NAME(faasm)

cls_handle_t h_class;
cls_method_handle_t h_maybe_exec_wasm_ro;
cls_method_handle_t h_maybe_exec_wasm_rw;
cls_method_handle_t h_maybe_exec_wasm_wo;

int maybe_exec_wasm_ro(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out);

int maybe_exec_wasm_rw(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out);

int maybe_exec_wasm_wo(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out);

CLS_INIT(faasm)
{
    CLS_LOG(0, "Loading Faasm-NDP Ceph interface class");

    cls_register("faasm", &h_class);

    // TODO: Determine what PROMOTE does
    cls_register_cxx_method(h_class,
                            "maybe_exec_wasm_ro",
                            CLS_METHOD_RD,
                            maybe_exec_wasm_ro,
                            &h_maybe_exec_wasm_ro);
    cls_register_cxx_method(h_class,
                            "maybe_exec_wasm_rw",
                            CLS_METHOD_RD | CLS_METHOD_WR,
                            maybe_exec_wasm_rw,
                            &h_maybe_exec_wasm_rw);
    cls_register_cxx_method(h_class,
                            "maybe_exec_wasm_wo",
                            CLS_METHOD_WR,
                            maybe_exec_wasm_wo,
                            &h_maybe_exec_wasm_wo);
}

namespace faasm {

namespace fbs = flatbuffers;

std::vector<uint8_t> cephBufferToVecU8(const ceph::buffer::list& buflist)
{
    const size_t totalSize = buflist.length();
    std::vector<uint8_t> output(totalSize);
    auto outIt = output.begin();
    for (const auto& buffer : buflist.buffers()) {
        outIt = std::copy_n(buffer.c_str(), buffer.length(), outIt);
    }
    return output;
}

int maybe_exec_wasm(cls_method_context_t hctx,
                    ceph::buffer::list* inBuffers,
                    ceph::buffer::list* outBuffers,
                    bool readAllowed,
                    bool writeAllowed)
{
    if (inBuffers == nullptr || outBuffers == nullptr ||
        !(readAllowed || writeAllowed)) {
        return -EINVAL;
    }
    CLS_LOG(3, "maybe_exec_wasm called");

    uint64_t callId = 0;
    ndpmsg::NdpResult result = ndpmsg::NdpResult_Ok;
    std::string errorMsg;

    try {
        CLS_LOG(5,
                "Received NDP call request of size %d bytes",
                (int)inBuffers->length());
        auto input = cephBufferToVecU8(*inBuffers);
        verifyFlatbuf<ndpmsg::NdpRequest>(input);
        const auto* req =
          flatbuffers::GetRoot<ndpmsg::NdpRequest>(input.data());
        callId = req->call_id();
        // Connect to the Faasm runtime
        CLS_LOG(5, "Connecting to Faasm runtime UDS");
        CephFaasmSocket runtime(SocketType::connect);
        // Forward the NDP request
        CLS_LOG(5,
                "Forwarding call request %llu to the Faasm runtime",
                static_cast<unsigned long long>(callId));
        runtime.sendMessage(input.data(), input.size());
        const auto initialRuntimeResponseData = runtime.recvMessageVector();
        verifyFlatbuf<ndpmsg::NdpResponse>(initialRuntimeResponseData);
        const auto* initialRuntimeResponse =
          flatbuffers::GetRoot<ndpmsg::NdpResponse>(
            initialRuntimeResponseData.data());
        if (initialRuntimeResponse->call_id() != callId) {
            throw std::runtime_error("Mismatched call ids!");
        }
        if (initialRuntimeResponse->result() != ndpmsg::NdpResult_Ok) {
            // Forward the error and finish.
            outBuffers->append(
              reinterpret_cast<const char*>(initialRuntimeResponseData.data()),
              initialRuntimeResponseData.size());
            return 0;
        }

        // Communicate with the runtime to process any read/write commands
        bool running = true;
        ceph::buffer::list readBufferList;
        while (running) {
            readBufferList.clear();
            const auto msgData = runtime.recvMessageVector();
            verifyFlatbuf<ndpmsg::StorageMessage>(msgData);
            const auto* topMsg =
              flatbuffers::GetRoot<ndpmsg::StorageMessage>(msgData.data());
            if (topMsg->call_id() != callId) {
                throw std::runtime_error(
                  "Mismatched call ids in storage message!");
            }
            switch (topMsg->message_type()) {
                case ndpmsg::TypedStorageMessage_NdpEnd: {
                    running = false;
                    break;
                }
                case ndpmsg::TypedStorageMessage_NdpRead: {
                    const auto* msg = topMsg->message_as_NdpRead();
                    int ec = cls_cxx_read(
                      hctx, msg->offset(), msg->upto_length(), &readBufferList);
                    if (ec < 0) {
                        runtime.sendError();
                        throw std::runtime_error("Error in a read operation");
                    }
                    runtime.sendMessage(
                      reinterpret_cast<const uint8_t*>(readBufferList.c_str()),
                      readBufferList.length());
                    break;
                }
                case ndpmsg::TypedStorageMessage_NdpWrite: {
                    const auto* msg = topMsg->message_as_NdpWrite();
                    // Const cast safety: we do not modify the data in the
                    // buffer.
                    auto writeBufferList = ceph::bufferlist::static_from_mem(
                      const_cast<char*>(
                        reinterpret_cast<const char*>(msg->data()->data())),
                      msg->data()->size());
                    int ec = cls_cxx_write(hctx,
                                           msg->offset(),
                                           msg->data()->size(),
                                           &writeBufferList);
                    if (ec < 0) {
                        runtime.sendError();
                        throw std::runtime_error("Error in a write operation");
                    }
                    break;
                }
                default: {
                    runtime.sendError();
                    throw std::runtime_error("Invalid storage message type");
                }
            }
        }
    } catch (const std::exception& e) {
        result = ndpmsg::NdpResult_Error;
        errorMsg = fmt::format(
          FMT_STRING("Exception caught in maybe_exec_wasm call {}: {}"),
          callId,
          e.what());
        CLS_LOG(1, "%s", errorMsg.c_str());
    } catch (...) {
        result = ndpmsg::NdpResult_Error;
        errorMsg = fmt::format(
          FMT_STRING(
            "Unknown exception type caught in maybe_exec_wasm call {}"),
          callId);
        CLS_LOG(1, "%s", errorMsg.c_str());
    }

    // Construct response
    flatbuffers::FlatBufferBuilder builder(256);
    auto errorField = builder.CreateString(errorMsg);
    ndpmsg::NdpResponseBuilder respBuilder(builder);
    respBuilder.add_call_id(callId);
    respBuilder.add_result(result);
    respBuilder.add_error_msg(errorField);

    auto resp = respBuilder.Finish();
    builder.Finish(resp);
    outBuffers->append(
      reinterpret_cast<const char*>(builder.GetBufferPointer()),
      builder.GetSize());

    return (result == ndpmsg::NdpResult_Ok) ? 0 : -EFAULT;
}

}

int maybe_exec_wasm_ro(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out)
{
    return faasm::maybe_exec_wasm(hctx, in, out, true, false);
}

int maybe_exec_wasm_rw(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out)
{
    return faasm::maybe_exec_wasm(hctx, in, out, true, true);
}

int maybe_exec_wasm_wo(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out)
{
    return faasm::maybe_exec_wasm(hctx, in, out, false, true);
}
