#include "runner_common.h"
#include "cephcomm_generated.h"

#include <asm-generic/errno.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/detail/error_code.hpp>
#include <fcntl.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <functional>
#include <memory>

#include <cephclass/cephcomm.h>
#include <faabric/util/asio.h>
#include <faabric/util/logging.h>
#include <stdexcept>
#include <wavm/WAVMWasmModule.h>

namespace runner {

void commonInit()
{
    wasm::setupWavmHooks();
}

class NdpEndpoint;

class NdpConnection : public std::enable_shared_from_this<NdpConnection>
{
  public:
    NdpConnection(asio::io_context& ioc,
                  std::shared_ptr<faasm::CephFaasmSocket> inConnection,
                  std::shared_ptr<NdpEndpoint> endpoint)
      : ioc(ioc)
      , connection(std::move(inConnection))
      , endpoint(std::move(endpoint))
      , sockConn(asio::make_strand(ioc))
    {
        // Make the socket non-blocking
        int flags = ::fcntl(connection->getFd(), F_GETFL);
        if (flags < 0) {
            perror("Couldn't get ndp connection flags");
            throw std::runtime_error("Couldn't get ndp connection flags");
        }
        flags &= ~O_NONBLOCK;
        int ec = ::fcntl(connection->getFd(), F_SETFL, flags);
        if (ec < 0) {
            perror("Couldn't unset O_NONBLOCK on the ndp connection flags");
            throw std::runtime_error(
              "Couldn't unset O_NONBLOCK on the ndp connection flags");
        }
        // Pass to Asio
        sockConn.assign(asio::local::stream_protocol{}, connection->getFd());
    }

    ~NdpConnection() { sockConn.release(); }

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
        sockConn.async_wait(
          asio::local::stream_protocol::acceptor::wait_type::wait_read,
          std::bind_front(&NdpConnection::onReceivable,
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
        using namespace faasm;
        namespace fbs = flatbuffers;
        if (!ec) {
            auto msgData = connection->recvMessageVector();
            faasm::verifyFlatbuf<ndpmsg::NdpRequest>(msgData);
            this->ndpRequestData = std::move(msgData);
            this->ndpRequest = flatbuffers::GetRoot<ndpmsg::NdpRequest>(
              this->ndpRequestData.data());

            auto flatBuilder = fbs::FlatBufferBuilder(128);
            auto responseBuilder = ndpmsg::NdpResponseBuilder(flatBuilder);
            responseBuilder.add_call_id(ndpRequest->call_id());
            responseBuilder.add_result(ndpmsg::NdpResult_Ok);
            auto responseOffset = responseBuilder.Finish();
            flatBuilder.Finish(responseOffset);
            connection->sendMessage(flatBuilder.GetBufferPointer(),
                                   flatBuilder.GetSize());

            doRecv();
        } else {
            SPDLOG_ERROR(
              "Error waiting for first recv on the ndp connection: {}",
              ec.to_string());
        }
    }

    // Handles one message response
    void onReceivable(const boost::system::error_code& ec)
    {
        if (ndpRequest == nullptr) {
            throw std::logic_error("ndpRequest is null in onReceivable");
        }
        using namespace faasm;
        namespace fbs = flatbuffers;
        if (!ec) {
            auto msgData = connection->recvMessageVector();
            // TODO
        } else {
            SPDLOG_ERROR("Error waiting for recv on the ndp connection: {}",
                         ec.to_string());
        }
    }

    asio::io_context& ioc;
    std::shared_ptr<faasm::CephFaasmSocket> connection;
    std::shared_ptr<NdpEndpoint> endpoint;
    asio::local::stream_protocol::socket sockConn;

    std::vector<uint8_t> ndpRequestData;
    const faasm::ndpmsg::NdpRequest* ndpRequest = nullptr;
};

class NdpEndpoint : public std::enable_shared_from_this<NdpEndpoint>
{
  public:
    NdpEndpoint(asio::io_context& ioc)
      : ioc(ioc)
      , socket(faasm::SocketType::listen)
      , sockAccept(asio::make_strand(ioc))
    {
        // Make the socket non-blocking
        int flags = ::fcntl(socket.getFd(), F_GETFL);
        if (flags < 0) {
            perror("Couldn't get ndp socket flags");
            throw std::runtime_error("Couldn't get ndp socket flags");
        }
        flags |= O_NONBLOCK;
        int ec = ::fcntl(socket.getFd(), F_SETFL, flags);
        if (ec < 0) {
            perror("Couldn't set O_NONBLOCK on the ndp socket flags");
            throw std::runtime_error(
              "Couldn't set O_NONBLOCK on the ndp socket flags");
        }
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
                  std::make_shared<faasm::CephFaasmSocket>(socket.accept());
                std::shared_ptr connHandler = std::make_shared<NdpConnection>(
                  ioc, std::move(connection), this->shared_from_this());
                connHandler->run();
            } catch (const std::runtime_error& err) {
                if (errno == EWOULDBLOCK) {
                    return doAccept();
                }
                throw;
            }
        } else {
            SPDLOG_ERROR("Error waiting for accept on the ndp endpoint: {}",
                         ec.to_string());
        }
    }

    asio::io_context& ioc;
    faasm::CephFaasmSocket socket;
    asio::local::stream_protocol::acceptor sockAccept;
};

std::function<void(asio::io_context&)> getNdpEndpoint()
{
    return [](asio::io_context& ioc) {
        std::shared_ptr ndpExec = std::make_shared<NdpEndpoint>(ioc);
        ndpExec->run();
    };
}

}
