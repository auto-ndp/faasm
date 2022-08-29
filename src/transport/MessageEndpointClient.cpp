#include <faabric/transport/MessageEndpointClient.h>
#include <faabric/util/timing.h>
#include <vector>

namespace faabric::transport {

MessageEndpointClient::MessageEndpointClient(std::string hostIn,
                                             int asyncPortIn,
                                             int syncPortIn,
                                             int timeoutMs)
  : host(hostIn)
  , asyncPort(asyncPortIn)
  , syncPort(syncPortIn)
  , asyncEndpoint(host, asyncPort, timeoutMs)
  , syncEndpoint(host, syncPort, timeoutMs)
{}

namespace {
thread_local std::vector<uint8_t> msgBuffer;
}

void MessageEndpointClient::asyncSend(int header,
                                      google::protobuf::Message* msg)
{
    ZoneScopedNS("MessageEndpointClient::asyncSend@2", 6);
    size_t msgSize = msg->ByteSizeLong();
    msgBuffer.resize(msgSize);
    ZoneValue(msgSize);

    TracyMessageL("Serialized");
    if (!msg->SerializeToArray(msgBuffer.data(), msgBuffer.size())) {
        throw std::runtime_error("Error serialising message");
    }

    asyncSend(header, msgBuffer.data(), msgBuffer.size());
}

void MessageEndpointClient::asyncSend(int header,
                                      const uint8_t* buffer,
                                      size_t bufferSize)
{
    ZoneScopedNS("MessageEndpointClient::asyncSend@3", 6);
    ZoneValue(bufferSize);
    asyncEndpoint.send(header, buffer, bufferSize);
}

void MessageEndpointClient::syncSend(int header,
                                     google::protobuf::Message* msg,
                                     google::protobuf::Message* response)
{
    ZoneScopedNS("MessageEndpointClient::syncSend@3", 6);
    size_t msgSize = msg->ByteSizeLong();
    ZoneValue(msgSize);
    msgBuffer.resize(msgSize);
    if (!msg->SerializeToArray(msgBuffer.data(), msgBuffer.size())) {
        throw std::runtime_error("Error serialising message");
    }
    TracyMessageL("Serialized");

    syncSend(header, msgBuffer.data(), msgBuffer.size(), response);
}

void MessageEndpointClient::syncSend(int header,
                                     const uint8_t* buffer,
                                     const size_t bufferSize,
                                     google::protobuf::Message* response)
{
    ZoneScopedNS("MessageEndpointClient::syncSend@4", 6);
    ZoneValue(bufferSize);

    Message responseMsg = syncEndpoint.sendAwaitResponse(header, buffer, bufferSize);
    TracyMessageL("Response sent");

    // Deserialise response
    if (!response->ParseFromArray(responseMsg.data(), responseMsg.size())) {
        throw std::runtime_error("Error deserialising message");
    }
}
}
