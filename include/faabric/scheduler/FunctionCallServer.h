#pragma once

#include <faabric/proto/faabric.pb.h>
#include <faabric/scheduler/FunctionCallApi.h>
#include <faabric/scheduler/Scheduler.h>
#include <faabric/transport/MessageEndpointServer.h>

#include <functional>

namespace faabric::scheduler {
class FunctionCallServer final
  : public faabric::transport::MessageEndpointServer
{
  public:
    FunctionCallServer();

    static void registerNdpDeltaHandler(int id, std::function<std::vector<uint8_t>()> handler);

    static void removeNdpDeltaHandler(int id);

  private:
    Scheduler& scheduler;

    void doAsyncRecv(transport::Message& message) override;

    std::unique_ptr<google::protobuf::Message> doSyncRecv(
      transport::Message& message) override;

    std::unique_ptr<google::protobuf::Message> recvFlush(
      std::span<const uint8_t> buffer);

    std::unique_ptr<google::protobuf::Message> recvGetResources(
      std::span<const uint8_t> buffer);

    std::unique_ptr<google::protobuf::Message> recvPendingMigrations(
      std::span<const uint8_t> buffer);

    std::unique_ptr<google::protobuf::Message> recvNdpDeltaRequest(
      std::span<const uint8_t> buffer);

    void recvExecuteFunctions(std::span<const uint8_t> buffer);

    void recvUnregister(std::span<const uint8_t> buffer);

    void recvDirectResult(std::span<const uint8_t> buffer);
};
}
