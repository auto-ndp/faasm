#pragma once

#include <array>
#include <cstdint>
#include <flatbuffers/flatbuffer_builder.h>
#include <functional>
#include <span>
#include <sstream>
#include <string>
#include <tl/expected.hpp>
#include <vector>

#include <cephclass/cephcomm.h>
#include <faabric/util/asio.h>
#include <faabric/util/bytes.h>

namespace faasm {

struct CephSocketCloser
{
  public:
    CephSocketCloser() = default;
    CephSocketCloser(const CephSocketCloser&) = delete;
    CephSocketCloser& operator=(const CephSocketCloser&) = delete;
    CephSocketCloser(CephSocketCloser&&) = default;
    CephSocketCloser& operator=(CephSocketCloser&&) = default;
    CephSocketCloser(std::shared_ptr<CephFaasmSocket> socket, uint64_t id)
      : socket(std::move(socket))
      , id(id)
    {
    }

    ~CephSocketCloser();

    std::shared_ptr<CephFaasmSocket> socket;
    uint64_t id;
};

std::function<void(asio::io_context&)> getNdpEndpoint();

std::shared_ptr<CephFaasmSocket> getNdpSocketFromCall(uint64_t id);

tl::expected<std::vector<uint8_t>, std::exception_ptr> awaitNdpResponse(
  uint64_t id);

}
