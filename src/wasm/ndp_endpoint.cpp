#include "faabric/util/locks.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_category.hpp>
#include <boost/system/detail/error_code.hpp>
#include <condition_variable>
#include <exception>
#include <flatbuffers/flatbuffer_builder.h>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>

#include <cephcomm_generated.h>
#include <faabric/scheduler/Scheduler.h>
#include <faabric/util/asio.h>
#include <faabric/util/concurrent_map.h>
#include <faabric/util/func.h>
#include <faabric/util/logging.h>
#include <wasm/ndp.h>

namespace faasm {

class NdpEndpoint;
class NdpConnection;

static faabric::util::ConcurrentMap<uint64_t, std::weak_ptr<NdpConnection>>
  ndpSocketMap;

class NdpConnection : public std::enable_shared_from_this<NdpConnection>
{
  public:
    NdpConnection(asio::io_context& ioc,
                  std::shared_ptr<CephFaasmSocket> inConnection,
                  std::shared_ptr<NdpEndpoint> endpoint)
      : ioc(ioc)
      , connection(std::move(inConnection))
      , endpoint(std::move(endpoint))
      , sockConn(asio::make_strand(ioc))
    {
        // Make the socket blocking
        connection->setBlocking(true);
        // Pass to Asio
        sockConn.assign(asio::local::stream_protocol{}, connection->getFd());
    }

    ~NdpConnection()
    {
        sockConn.release();
        if (ndpRequest != nullptr) {
            SPDLOG_DEBUG("Releasing ndp socket for {}",
                         ndpRequest->request_nested_root()->call_id());
            ndpSocketMap.erase(ndpRequest->request_nested_root()->call_id());
        }
    }

    NdpConnection(const NdpConnection&) = delete;
    NdpConnection& operator=(const NdpConnection&) = delete;

    void run()
    {
        asio::dispatch(sockConn.get_executor(),
                       std::bind_front(&NdpConnection::doFirstRecv,
                                       this->shared_from_this()));
    }

  private:
    void doRecv()
    {
        asio::async_read(sockConn,
                         asio::buffer(&nextMsgSize, sizeof(uint64_t)),
                         std::bind_front(&NdpConnection::recvMsgContent,
                                         this->shared_from_this()));
    }

    void doFirstRecv()
    {
        sockConn.async_wait(
          asio::local::stream_protocol::acceptor::wait_type::wait_read,
          std::bind_front(&NdpConnection::onFirstReceivable,
                          this->shared_from_this()));
    }

    // Handles one message
    void onFirstReceivable(const boost::system::error_code& ec)
    {
        namespace fbs = flatbuffers;
        if (!ec) {
            auto msgData = connection->recvMessageVector();
            verifyFlatbuf<ndpmsg::CephNdpRequest>(msgData);
            this->ndpRequestData = std::move(msgData);
            this->ndpRequest = flatbuffers::GetRoot<ndpmsg::CephNdpRequest>(
              this->ndpRequestData.data());

            auto& sch = faabric::scheduler::getScheduler();
            const bool hasCapacity = sch.executionSlotsSemaphore.try_acquire();
            auto ndpResult = hasCapacity ? ndpmsg::NdpResult_Ok
                                         : ndpmsg::NdpResult_ProcessLocally;
            std::string ndpError;
            if (hasCapacity) {
                // TODO: Keep a token until claimed by the runtime to prevent
                // oversubscription
                sch.executionSlotsSemaphore.release();
                try {

                    if (!ndpSocketMap.tryEmplace(
                          ndpRequest->request_nested_root()->call_id(),
                          this->shared_from_this())) {
                        throw std::runtime_error("Duplicate NDP call id");
                    }

                    auto msg = faabric::util::messageFactory(
                      ndpRequest->request_nested_root()->wasm()->user()->str(),
                      ndpRequest->request_nested_root()
                        ->wasm()
                        ->function()
                        ->str());
                    msg.set_id(ndpRequest->request_nested_root()->call_id());
                    msg.set_appid(ndpRequest->request_nested_root()->call_id());

                    msg.set_funcptr(
                      ndpRequest->request_nested_root()->wasm()->fptr());
                    msg.set_ispython(ndpRequest->request_nested_root()
                                       ->wasm()
                                       ->pyptr()
                                       ->size() > 0);
                    msg.set_pythonfunction(ndpRequest->request_nested_root()
                                             ->wasm()
                                             ->pyptr()
                                             ->str());
                    msg.set_pythonuser(
                      ndpRequest->request_nested_root()->wasm()->user()->str());

                    msg.set_isstorage(true);
                    msg.set_isoutputmemorydelta(true);
                    msg.set_directresulthost(
                      ndpRequest->request_nested_root()->origin_host()->str());
                    msg.mutable_wasmglobals()->Assign(
                      ndpRequest->request_nested_root()
                        ->wasm()
                        ->globals()
                        ->cbegin(),
                      ndpRequest->request_nested_root()
                        ->wasm()
                        ->globals()
                        ->cend());
                    msg.mutable_extraarguments()->Assign(
                      ndpRequest->request_nested_root()
                        ->wasm()
                        ->args()
                        ->cbegin(),
                      ndpRequest->request_nested_root()
                        ->wasm()
                        ->args()
                        ->cend());
                    msg.set_ndpcallobjectname(ndpRequest->request_nested_root()
                                                ->object()
                                                ->key()
                                                ->str());
                    sch.callFunction(
                      msg,
                      true,
                      {},
                      std::make_shared<CephSocketCloser>(
                        connection,
                        ndpRequest->request_nested_root()->call_id()));
                } catch (std::exception& e) {
                    ndpError = e.what();
                    SPDLOG_ERROR(
                      "Exception when scheduling an NDP function: {}",
                      ndpError);
                    ndpResult = ndpmsg::NdpResult_Error;
                }
            }
            SPDLOG_DEBUG("Handling request {} near-storage from OSD {}: {}",
                         ndpRequest->request_nested_root()->call_id(),
                         ndpRequest->osd_name()->str(),
                         hasCapacity);

            auto flatBuilder = fbs::FlatBufferBuilder(128);
            auto responseError = flatBuilder.CreateString(ndpError);
            auto responseBuilder = ndpmsg::NdpResponseBuilder(flatBuilder);
            responseBuilder.add_call_id(
              ndpRequest->request_nested_root()->call_id());
            responseBuilder.add_result(ndpResult);
            responseBuilder.add_error_msg(responseError);
            auto responseOffset = responseBuilder.Finish();
            flatBuilder.Finish(responseOffset);
            connection->sendMessage(flatBuilder.GetBufferPointer(),
                                    flatBuilder.GetSize());

            if (hasCapacity) {
                doRecv();
            }
        } else {
            SPDLOG_ERROR(
              "Error waiting for first recv on the ndp connection: {}",
              ec.to_string());
        }
    }

    void recvMsgContent(const boost::system::error_code& ec,
                        size_t bytesTransferred)
    {
        if (!ec) {
            nextMsgSize = ::htole64(nextMsgSize);
            nextMsg.resize(nextMsgSize);
            asio::async_read(sockConn,
                             asio::buffer(nextMsg.data(), nextMsg.size()),
                             std::bind_front(&NdpConnection::onReceivable,
                                             this->shared_from_this()));
        } else if (ec.value() != boost::system::errc::operation_canceled) {
            SPDLOG_ERROR("Error waiting for recv on the ndp connection: {}",
                         ec.to_string());
        }
    }

    // Handles one message response
    void onReceivable(const boost::system::error_code& ec,
                      size_t bytesTransferred)
    {
        if (ndpRequest == nullptr) {
            throw std::logic_error("ndpRequest is null in onReceivable");
        }
        namespace fbs = flatbuffers;
        if (!ec) {
            decltype(lastMessage) msgToStore;
            msgToStore = std::move(nextMsg);
            {
                faabric::util::UniqueLock lock{ lastMessageMx };
                lastMessage.swap(msgToStore);
            }
            lastMessageFlag.test_and_set();
            lastMessageFlag.notify_one();

            doRecv();
        } else if (ec.value() != boost::system::errc::operation_canceled) {
            SPDLOG_ERROR("Error waiting for recv on the ndp connection: {}",
                         ec.to_string());
        }
    }

  public:
    asio::io_context& ioc;
    std::shared_ptr<CephFaasmSocket> connection;
    std::shared_ptr<NdpEndpoint> endpoint;
    asio::local::stream_protocol::socket sockConn;

    uint64_t nextMsgSize;
    std::vector<uint8_t> nextMsg;

    tl::expected<std::vector<uint8_t>, std::exception_ptr> lastMessage;
    std::mutex lastMessageMx;
    std::atomic_flag lastMessageFlag;

    std::vector<uint8_t> ndpRequestData;
    const ndpmsg::CephNdpRequest* ndpRequest = nullptr;
};

class NdpEndpoint : public std::enable_shared_from_this<NdpEndpoint>
{
  public:
    NdpEndpoint(asio::io_context& ioc)
      : ioc(ioc)
      , socket(SocketType::listen)
      , sockAccept(asio::make_strand(ioc))
    {
        // Make the socket non-blocking
        socket.setBlocking(false);
        // Pass to Asio
        sockAccept.assign(asio::local::stream_protocol{}, socket.getFd());
    }

    ~NdpEndpoint() { sockAccept.release(); }

    NdpEndpoint(const NdpEndpoint&) = delete;
    NdpEndpoint& operator=(const NdpEndpoint&) = delete;

    void run()
    {
        asio::dispatch(
          sockAccept.get_executor(),
          std::bind_front(&NdpEndpoint::doAccept, this->shared_from_this()));
    }

  private:
    void doAccept()
    {
        sockAccept.async_wait(
          asio::local::stream_protocol::acceptor::wait_type::wait_read,
          std::bind_front(&NdpEndpoint::onAcceptable,
                          this->shared_from_this()));
    }

    void onAcceptable(const boost::system::error_code& ec)
    {
        if (!ec) {
            try {
                auto connection =
                  std::make_shared<CephFaasmSocket>(socket.accept());
                std::shared_ptr connHandler = std::make_shared<NdpConnection>(
                  ioc, std::move(connection), this->shared_from_this());
                connHandler->run();
            } catch (const std::runtime_error& err) {
                if (errno == EWOULDBLOCK) {
                    return doAccept();
                }
                throw;
            }
            doAccept();
        } else {
            SPDLOG_ERROR("Error waiting for accept on the ndp endpoint: {}",
                         ec.to_string());
        }
    }

    asio::io_context& ioc;
    CephFaasmSocket socket;
    asio::local::stream_protocol::acceptor sockAccept;
};

std::function<void(asio::io_context&)> getNdpEndpoint()
{
    ndpSocketMap.reserve(512);
    return [](asio::io_context& ioc) {
        std::shared_ptr ndpExec = std::make_shared<NdpEndpoint>(ioc);
        ndpExec->run();
    };
}

std::shared_ptr<CephFaasmSocket> getNdpSocketFromCall(uint64_t id)
{
    auto weak = ndpSocketMap.get(id).value();
    return std::shared_ptr(weak)->connection;
}

tl::expected<std::vector<uint8_t>, std::exception_ptr> awaitNdpResponse(
  uint64_t id)
{
    auto weak = ndpSocketMap.get(id).value();
    auto sock = std::shared_ptr(weak);
    while (true) {
        sock->lastMessageFlag.wait(false);
        {
            faabric::util::UniqueLock lock{ sock->lastMessageMx };
            if (!sock->lastMessageFlag.test()) {
                continue;
            }
            sock->lastMessageFlag.clear();
            return std::move(sock->lastMessage);
        }
    }
}

CephSocketCloser::~CephSocketCloser()
{
    if (socket != nullptr) {
        SPDLOG_DEBUG("Closing Ceph socket for {}", id);
        flatbuffers::FlatBufferBuilder builder(64);
        auto endField = ndpmsg::CreateNdpEnd(builder);
        auto endMsg = ndpmsg::CreateStorageMessage(
          builder, id, ndpmsg::TypedStorageMessage_NdpEnd, endField.Union());
        builder.Finish(endMsg);
        socket->sendMessage(builder.GetBufferPointer(), builder.GetSize());

        auto conn = ndpSocketMap.get(id)->lock();
        if (conn != nullptr) {
            conn->sockConn.cancel();
        }

        ::shutdown(socket->getFd(), SHUT_RDWR);
        socket = nullptr;
    }
}

}