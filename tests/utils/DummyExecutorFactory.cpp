#include "DummyExecutorFactory.h"
#include "DummyExecutor.h"

#include <faabric/util/logging.h>

namespace faabric::scheduler {

std::shared_ptr<Executor> DummyExecutorFactory::createExecutor(
  faabric::MessageInBatch msg)
{
    return std::make_shared<DummyExecutor>(std::move(msg));
}

int DummyExecutorFactory::getFlushCount()
{
    return flushCount;
}

void DummyExecutorFactory::flushHost()
{
    flushCount++;
}

void DummyExecutorFactory::reset()
{
    flushCount = 0;
}
}
