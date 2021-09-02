#include <faabric/proto/faabric.pb.h>
#include <faabric/scheduler/FunctionCallClient.h>
#include <faabric/transport/common.h>
#include <faabric/transport/macros.h>
#include <faabric/util/queue.h>
#include <faabric/util/testing.h>

namespace faabric::scheduler {

// -----------------------------------
// Mocking
// -----------------------------------
std::mutex mockMutex;

static std::vector<std::pair<std::string, faabric::Message>> functionCalls;

static std::vector<std::pair<std::string, faabric::EmptyRequest>> flushCalls;

static std::vector<
  std::pair<std::string, std::shared_ptr<faabric::BatchExecuteRequest>>>
  batchMessages;

static std::vector<std::pair<std::string, faabric::EmptyRequest>>
  resourceRequests;

static std::unordered_map<std::string,
                          faabric::util::Queue<faabric::HostResources>>
  queuedResourceResponses;

static std::vector<std::pair<std::string, faabric::UnregisterRequest>>
  unregisterRequests;

std::vector<std::pair<std::string, faabric::Message>> getFunctionCalls()
{
    return functionCalls;
}

std::vector<std::pair<std::string, faabric::EmptyRequest>> getFlushCalls()
{
    return flushCalls;
}

std::vector<
  std::pair<std::string, std::shared_ptr<faabric::BatchExecuteRequest>>>
getBatchRequests()
{
    return batchMessages;
}

std::vector<std::pair<std::string, faabric::EmptyRequest>> getResourceRequests()
{
    return resourceRequests;
}

std::vector<std::pair<std::string, faabric::UnregisterRequest>>
getUnregisterRequests()
{
    return unregisterRequests;
}

void queueResourceResponse(const std::string& host, faabric::HostResources& res)
{
    queuedResourceResponses[host].enqueue(res);
}

void clearMockRequests()
{
    functionCalls.clear();
    batchMessages.clear();
    resourceRequests.clear();
    unregisterRequests.clear();

    for (auto& p : queuedResourceResponses) {
        p.second.reset();
    }
    queuedResourceResponses.clear();
}

// -----------------------------------
// Message Client
// -----------------------------------
FunctionCallClient::FunctionCallClient(const std::string& hostIn)
  : faabric::transport::MessageEndpointClient(hostIn,
                                              FUNCTION_CALL_ASYNC_PORT,
                                              FUNCTION_CALL_SYNC_PORT)
{}

void FunctionCallClient::sendFlush()
{
    faabric::EmptyRequest req;
    if (faabric::util::isMockMode()) {
        faabric::util::UniqueLock lock(mockMutex);
        flushCalls.emplace_back(host, req);
    } else {
        faabric::EmptyResponse resp;
        syncSend(faabric::scheduler::FunctionCalls::Flush, &req, &resp);
    }
}

faabric::HostResources FunctionCallClient::getResources()
{
    faabric::EmptyRequest request;
    faabric::HostResources response;

    if (faabric::util::isMockMode()) {
        faabric::util::UniqueLock lock(mockMutex);

        // Register the request
        resourceRequests.emplace_back(host, request);

        // See if we have a queued response
        if (queuedResourceResponses[host].size() > 0) {
            response = queuedResourceResponses[host].dequeue();
        }
    } else {
        syncSend(
          faabric::scheduler::FunctionCalls::GetResources, &request, &response);
    }

    return response;
}

void FunctionCallClient::executeFunctions(
  const std::shared_ptr<faabric::BatchExecuteRequest> req)
{
    if (faabric::util::isMockMode()) {
        faabric::util::UniqueLock lock(mockMutex);
        batchMessages.emplace_back(host, req);
    } else {
        asyncSend(faabric::scheduler::FunctionCalls::ExecuteFunctions,
                  req.get());
    }
}

void FunctionCallClient::sendDirectResult(faabric::Message msg)
{
    faabric::DirectResultTransmission drt;
    drt.mutable_result()->CopyFrom(msg);
    asyncSend(faabric::scheduler::FunctionCalls::DirectResult, &drt);
}

void FunctionCallClient::unregister(faabric::UnregisterRequest& req)
{
    if (faabric::util::isMockMode()) {
        faabric::util::UniqueLock lock(mockMutex);
        unregisterRequests.emplace_back(host, req);
    } else {
        asyncSend(faabric::scheduler::FunctionCalls::Unregister, &req);
    }
}
}
