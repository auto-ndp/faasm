#pragma once

#include <memory>

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

    Message* operator->() { return &msg; }
    const Message* operator->() const { return &msg; }
    Message& operator*() { return msg; }
    const Message& operator*() const { return msg; }
    operator Message&() { return msg; }
    operator const Message&() const { return msg; }
};

}
