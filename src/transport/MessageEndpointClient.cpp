#include <faabric/transport/MessageEndpointClient.h>
#include <faabric/util/timing.h>

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

void MessageEndpointClient::asyncSend(int header,
                                      google::protobuf::Message* msg)
{
    ZoneScopedNS("MessageEndpointClient::asyncSend@2", 6);
    size_t msgSize = msg->ByteSizeLong();
    uint8_t buffer[msgSize];

    TracyMessageL("Serialized");
    if (!msg->SerializeToArray(buffer, msgSize)) {
        throw std::runtime_error("Error serialising message");
    }

    asyncSend(header, buffer, msgSize);
}

void MessageEndpointClient::asyncSend(int header,
                                      const uint8_t* buffer,
                                      size_t bufferSize)
{
    ZoneScopedNS("MessageEndpointClient::asyncSend@3", 6);
    asyncEndpoint.sendHeader(header);
    TracyMessageL("Header sent");
    asyncEndpoint.send(buffer, bufferSize);
}

void MessageEndpointClient::syncSend(int header,
                                     google::protobuf::Message* msg,
                                     google::protobuf::Message* response)
{
    ZoneScopedNS("MessageEndpointClient::syncSend@3", 6);
    size_t msgSize = msg->ByteSizeLong();
    uint8_t buffer[msgSize];
    if (!msg->SerializeToArray(buffer, msgSize)) {
        throw std::runtime_error("Error serialising message");
    }
    TracyMessageL("Serialized");

    syncSend(header, buffer, msgSize, response);
}

void MessageEndpointClient::syncSend(int header,
                                     const uint8_t* buffer,
                                     const size_t bufferSize,
                                     google::protobuf::Message* response)
{
    ZoneScopedNS("MessageEndpointClient::syncSend@4", 6);
    syncEndpoint.sendHeader(header);
    TracyMessageL("Header sent");

    Message responseMsg = syncEndpoint.sendAwaitResponse(buffer, bufferSize);
    TracyMessageL("Response sent");

    // Deserialise response
    if (!response->ParseFromArray(responseMsg.data(), responseMsg.size())) {
        throw std::runtime_error("Error deserialising message");
    }
}
}
