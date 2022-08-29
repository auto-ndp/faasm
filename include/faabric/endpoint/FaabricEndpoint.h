#pragma once

#include <functional>
#include <memory>

#include <faabric/proto/faabric.pb.h>
#include <faabric/util/asio.h>
#include <faabric/util/config.h>
#include <pistache/endpoint.h>
#include <pistache/http.h>

namespace faabric::endpoint {

enum class EndpointMode
{
    SIGNAL,
    BG_THREAD
};

namespace detail {
struct EndpointState;
}

struct HttpRequestContext
{
    asio::io_context& ioc;
    asio::any_io_executor executor;
    std::function<void(faabric::util::BeastHttpResponse&&)> sendFunction;
};

class HttpRequestHandler
{
  public:
    virtual void onRequest(HttpRequestContext&& ctx,
                           faabric::util::BeastHttpRequest&& request) = 0;
};

class Endpoint
{
  public:
    Endpoint() = delete;
    Endpoint(const Endpoint&) = delete;
    Endpoint(Endpoint&&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    Endpoint& operator=(Endpoint&&) = delete;
    virtual ~Endpoint();

    Endpoint(int port,
             int threadCount,
             std::shared_ptr<HttpRequestHandler> requestHandlerIn);

    void start(EndpointMode mode = EndpointMode::SIGNAL);

    void stop();

  private:
    int port;
    int threadCount;
    std::unique_ptr<detail::EndpointState> state;
    std::shared_ptr<HttpRequestHandler> requestHandler;
};
}
