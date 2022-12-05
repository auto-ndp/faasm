#pragma once

#include <array>
#include <cstdint>
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

std::function<void(asio::io_context&)> getNdpEndpoint();

std::shared_ptr<CephFaasmSocket> getNdpSocketFromCall(uint64_t id);

tl::expected<std::vector<uint8_t>, std::exception_ptr> awaitNdpResponse(
  uint64_t id);

}
