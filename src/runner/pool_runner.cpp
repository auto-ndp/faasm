#include <conf/FaasmConfig.h>
#include <faaslet/Faaslet.h>
#include <storage/S3Wrapper.h>

#include <faabric/endpoint/Endpoint.h>
#include <faabric/endpoint/FaabricEndpointHandler.h>
#include <faabric/runner/FaabricMain.h>
#include <faabric/transport/context.h>
#include <faabric/util/config.h>
#include <faabric/util/logging.h>

#include "runner_common.h"

#include <sys/prctl.h>

int main()
{
    runner::commonInit();
    if (faabric::util::getSystemConfig().isStorageNode) {
        prctl(PR_SET_NAME, "pool_runner[S]");
    } else {
        prctl(PR_SET_NAME, "pool_runner[C]");
    }
    storage::initFaasmS3();
    faabric::transport::initGlobalMessageContext();
    faabric::util::initLogging();

    // WARNING: All 0MQ-related operations must take place in a self-contined
    // scope to ensure all sockets are destructed before closing the context.
    {
        auto fac = std::make_shared<faaslet::FaasletFactory>();
        faabric::runner::FaabricMain m(fac);
        m.startBackground();

        faabric::util::getSystemConfig().print();
        conf::getFaasmConfig().print();

        // Start endpoint (will also have multiple threads)
        SPDLOG_INFO("Starting endpoint");
        const auto& config = faabric::util::getSystemConfig();
        faabric::endpoint::Endpoint endpoint(
          config.endpointPort,
          config.endpointNumThreads,
          std::make_shared<faabric::endpoint::FaabricEndpointHandler>());
        endpoint.start();

        SPDLOG_INFO("Shutting down");
        m.shutdown();

        faabric::transport::closeGlobalMessageContext();
    }

    storage::shutdownFaasmS3();
    return 0;
}
