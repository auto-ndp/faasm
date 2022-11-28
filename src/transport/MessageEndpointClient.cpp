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
                                      google::protobuf::Message* msg,
                                      int sequenceNum)
{
    ZoneScopedNS("MessageEndpointClient::asyncSend@2", 6);
    std::string buffer;

    if (!msg->SerializeToString(&buffer)) {
        throw std::runtime_error("Error serialising message");
    }

    asyncSend(header,
              reinterpret_cast<uint8_t*>(buffer.data()),
              buffer.size(),
              sequenceNum);
}

void MessageEndpointClient::asyncSend(int header,
                                      const uint8_t* buffer,
                                      size_t bufferSize,
                                      int sequenceNum)
{
    ZoneScopedNS("MessageEndpointClient::asyncSend@3", 6);
    ZoneValue(bufferSize);
    asyncEndpoint.send(header, buffer, bufferSize, sequenceNum);
}

void MessageEndpointClient::syncSend(int header,
                                     google::protobuf::Message* msg,
                                     google::protobuf::Message* response)
{
    ZoneScopedNS("MessageEndpointClient::syncSend@3", 6);
    std::string buffer;
    if (!msg->SerializeToString(&buffer)) {
        throw std::runtime_error("Error serialising message");
    }

    syncSend(header,
             reinterpret_cast<uint8_t*>(buffer.data()),
             buffer.size(),
             response);
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
