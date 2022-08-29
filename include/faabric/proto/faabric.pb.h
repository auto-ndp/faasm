#pragma once

#include <memory>
#include <faabric/util/gids.h>

#include_next "faabric.pb.h"

namespace faabric {

// Wrapper for BatchExecuteRequest::messages[i] to avoid copying message storage
struct MessageInBatch final
{
    std::shared_ptr<BatchExecuteRequest> batch;
    Message& msg;

    MessageInBatch() = delete;
    MessageInBatch(std::shared_ptr<BatchExecuteRequest> batch, int idx)
      : batch(batch)
      , msg(batch->mutable_messages()->at(idx))
    {}
    explicit MessageInBatch(Message& rmsg) 
        : batch(initBatch(rmsg))
        , msg(batch->mutable_messages()->at(0))
    {}

    Message* operator->() { return &msg; }
    const Message* operator->() const { return &msg; }
    Message& operator*() { return msg; }
    const Message& operator*() const { return msg; }
    operator Message&() { return msg; }
    operator const Message&() const { return msg; }
private:
    static std::shared_ptr<BatchExecuteRequest> initBatch(Message& rmsg) {
        auto batch = std::make_shared<faabric::BatchExecuteRequest>();
        batch->set_id(faabric::util::generateGid());
        *batch->add_messages() = std::move(rmsg);
        return batch;
    }
};

}
