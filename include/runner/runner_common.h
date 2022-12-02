#pragma once

#include <boost/asio/io_context.hpp>
#include <faabric/util/asio.h>

namespace runner {

void commonInit();

std::function<void(asio::io_context&)> getNdpEndpoint();

}
