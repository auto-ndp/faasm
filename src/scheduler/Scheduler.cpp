#include <faabric/proto/faabric.pb.h>
#include <faabric/redis/Redis.h>
#include <faabric/scheduler/ExecutorFactory.h>
#include <faabric/scheduler/FunctionCallClient.h>
#include <faabric/scheduler/Scheduler.h>
#include <faabric/snapshot/SnapshotClient.h>
#include <faabric/snapshot/SnapshotRegistry.h>
#include <faabric/transport/PointToPointBroker.h>
#include <faabric/util/environment.h>
#include <faabric/util/func.h>
#include <faabric/util/locks.h>
#include <faabric/util/logging.h>
#include <faabric/util/memory.h>
#include <faabric/util/random.h>
#include <faabric/util/scheduling.h>
#include <faabric/util/snapshot.h>
#include <faabric/util/testing.h>
#include <faabric/util/timing.h>

#include <sys/syscall.h>
#include <sys/eventfd.h>
#include <sys/file.h>

#include <chrono>
#include <unordered_set>

#define FLUSH_TIMEOUT_MS 10000

using namespace faabric::util;
using namespace faabric::snapshot;

namespace faabric::scheduler {

// 0MQ sockets are not thread-safe, and opening them and closing them from
// different threads messes things up. However, we don't want to constatnly
// create and recreate them to make calls in the scheduler, therefore we cache
// them in TLS, and perform thread-specific tidy-up.
static thread_local std::unordered_map<std::string,
                                       faabric::scheduler::FunctionCallClient>
  functionCallClients;

static thread_local std::unordered_map<std::string,
                                       faabric::snapshot::SnapshotClient>
  snapshotClients;

MessageLocalResult::MessageLocalResult()
{
    event_fd = eventfd(0, EFD_CLOEXEC);
}

MessageLocalResult::~MessageLocalResult()
{
    if (event_fd >= 0) {
        close(event_fd);
    }
}

void MessageLocalResult::set_value(std::unique_ptr<faabric::Message>&& msg)
{
    this->promise.set_value(std::move(msg));
    eventfd_write(this->event_fd, (eventfd_t)1);
}

Scheduler& getScheduler()
{
    static Scheduler sch;
    return sch;
}

Scheduler::Scheduler()
  : thisHost(faabric::util::getSystemConfig().endpointHost)
  , conf(faabric::util::getSystemConfig())
  , broker(faabric::transport::getPointToPointBroker())
{
    // Set up the initial resources
    int cores = faabric::util::getUsableCores();
    thisHostResources.set_slots(cores);

    if (this->conf.isStorageNode) {
        redis::Redis& redis = redis::Redis::getQueue();
        redis.sadd(ALL_STORAGE_HOST_SET, this->thisHost);
    }

    if (!this->conf.schedulerMonitorFile.empty()) {
        this->monitorFd = open(conf.schedulerMonitorFile.c_str(),
                               O_RDWR | O_CREAT | O_NOATIME | O_TRUNC,
                               S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (this->monitorFd < 0) {
            perror("Couldn't open monitoring fd");
            this->monitorFd = -1;
        }
        this->updateMonitoring();
    }
}

std::set<std::string> Scheduler::getAvailableHosts()
{
    ZoneScopedN("Scheduler::getAvailableHosts");
    redis::Redis& redis = redis::Redis::getQueue();
    return redis.smembers(getGlobalSetName());
}

std::set<std::string> Scheduler::getAvailableHostsForFunction(
  const faabric::Message& msg)
{
    ZoneScopedN("Scheduler::getAvailableHostsForFunction");
    redis::Redis& redis = redis::Redis::getQueue();
    return redis.smembers(getGlobalSetNameForFunction(msg));
}

void Scheduler::addHostToGlobalSet(const std::string& host)
{
    redis::Redis& redis = redis::Redis::getQueue();
    redis.sadd(getGlobalSetName(), host);
}

void Scheduler::removeHostFromGlobalSet(const std::string& host)
{
    redis::Redis& redis = redis::Redis::getQueue();
    redis.srem(getGlobalSetName(), host);
}

void Scheduler::addHostToGlobalSet()
{
    this->addHostToGlobalSet(thisHost);
}

const char* Scheduler::getGlobalSetName() const
{
    return this->conf.isStorageNode ? AVAILABLE_STORAGE_HOST_SET
                                    : AVAILABLE_HOST_SET;
}

const char* Scheduler::getGlobalSetNameForFunction(
  const faabric::Message& msg) const
{
    return msg.isstorage() ? AVAILABLE_STORAGE_HOST_SET : AVAILABLE_HOST_SET;
}

void Scheduler::resetThreadLocalCache()
{
    auto tid = (pid_t)syscall(SYS_gettid);
    SPDLOG_DEBUG("Resetting scheduler thread-local cache for thread {}", tid);

    functionCallClients.clear();
    snapshotClients.clear();
}

void Scheduler::reset()
{
    SPDLOG_DEBUG("Resetting scheduler");

    resetThreadLocalCache();

    // Shut down all Executors
    for (auto& p : executors) {
        for (auto& e : p.second) {
            e->finish();
        }
    }

    for (auto& e : deadExecutors) {
        e->finish();
    }

    executors.clear();
    deadExecutors.clear();

    // Ensure host is set correctly
    thisHost = faabric::util::getSystemConfig().endpointHost;

    // Reset resources
    thisHostResources = faabric::HostResources();
    thisHostResources.set_slots(faabric::util::getUsableCores());

    // Reset scheduler state
    availableHostsCache.clear();
    registeredHosts.clear();
    threadResults.clear();

    // Records
    recordedMessagesAll.clear();
    recordedMessagesLocal.clear();
    recordedMessagesShared.clear();
}

void Scheduler::shutdown()
{
    reset();

    this->removeHostFromGlobalSet(thisHost);
    if (this->conf.isStorageNode) {
        redis::Redis& redis = redis::Redis::getQueue();
        redis.srem(ALL_STORAGE_HOST_SET, thisHost);
    }
}

long Scheduler::getFunctionExecutorCount(const faabric::Message& msg)
{
    const std::string funcStr = faabric::util::funcToString(msg, false);
    return executors[funcStr].size();
}

int Scheduler::getFunctionRegisteredHostCount(const faabric::Message& msg)
{
    const std::string funcStr = faabric::util::funcToString(msg, false);
    return (int)registeredHosts[funcStr].size();
}

std::set<std::string> Scheduler::getFunctionRegisteredHosts(
  const faabric::Message& msg)
{
    const std::string funcStr = faabric::util::funcToString(msg, false);
    return registeredHosts[funcStr];
}

void Scheduler::removeRegisteredHost(const std::string& host,
                                     const faabric::Message& msg)
{
    const std::string funcStr = faabric::util::funcToString(msg, false);
    registeredHosts[funcStr].erase(host);
}

void Scheduler::vacateSlot()
{
    ZoneScopedNS("Vacate scheduler slot", 5);
    thisHostUsedSlots.fetch_sub(1, std::memory_order_acq_rel);
}

void Scheduler::notifyExecutorShutdown(Executor* exec,
                                       const faabric::Message& msg)
{
    ZoneScopedNS("Scheduler::notifyExecutorShutdown", 5);
    faabric::util::FullLock lock(mx);

    SPDLOG_TRACE("Shutting down executor {}", exec->id);

    std::string funcStr = faabric::util::funcToString(msg, false);

    // Find in list of executors
    int execIdx = -1;
    std::vector<std::shared_ptr<Executor>>& thisExecutors = executors[funcStr];
    for (int i = 0; i < thisExecutors.size(); i++) {
        if (thisExecutors.at(i).get() == exec) {
            execIdx = i;
            break;
        }
    }

    if (execIdx < 0) {
        SPDLOG_ERROR("Couldn't find executor with id {}, current executor count: {}", exec->id, thisExecutors.size());
        return;
    }
    // We assume it's been found or something has gone very wrong
    assert(execIdx >= 0);

    // Record as dead, remove from live executors
    // Note that this is necessary as this method may be called from a worker
    // thread, so we can't fully clean up the executor without having a deadlock
    deadExecutors.emplace_back(thisExecutors.at(execIdx));
    thisExecutors.erase(thisExecutors.begin() + execIdx);

    if (thisExecutors.empty()) {
        SPDLOG_TRACE("No remaining executors for {}", funcStr);

        // Unregister if this was the last executor for that function
        bool isMaster = thisHost == msg.masterhost();
        if (!isMaster) {
            faabric::UnregisterRequest req;
            req.set_host(thisHost);
            *req.mutable_function() = msg;

            getFunctionCallClient(msg.masterhost()).unregister(req);
        }
    }
}

faabric::util::SchedulingDecision Scheduler::callFunctions(
  std::shared_ptr<faabric::BatchExecuteRequest> req,
  bool forceLocal,
  faabric::Message* caller)
{
    ZoneScopedNS("Scheduler::callFunctions", 5);
    auto& config = faabric::util::getSystemConfig();

    int nMessages = req->messages_size();
    bool isThreads = req->type() == faabric::BatchExecuteRequest::THREADS;

    // Note, we assume all the messages are for the same function and have the
    // same master host
    faabric::Message& firstMsg = req->mutable_messages()->at(0);
    std::string funcStr = faabric::util::funcToString(firstMsg, false);
    std::string masterHost = firstMsg.masterhost();
    if (masterHost.empty()) {
        std::string funcStrWithId = faabric::util::funcToString(firstMsg, true);
        SPDLOG_ERROR("Request {} has no master host", funcStrWithId);
        throw std::runtime_error("Message with no master host");
    }
    bool isStorage = firstMsg.isstorage();
    bool iAmStorage = config.isStorageNode;
    bool hostKindDifferent = (isStorage != iAmStorage);

    // Set up scheduling decision
    SchedulingDecision decision(firstMsg.appid(), firstMsg.groupid());

    // TODO - more granular locking, this is conservative
    faabric::util::FullLock lock(mx);

    // If we're not the master host, we need to forward the request back to the
    // master host. This will only happen if a nested batch execution happens.
    std::vector<int> localMessageIdxs;
    if (!forceLocal && masterHost != thisHost) {
        SPDLOG_DEBUG(
          "Forwarding {} {} back to master {}", nMessages, funcStr, masterHost);

        ZoneScopedN("Scheduler::callFunctions forward to master");
        getFunctionCallClient(masterHost).executeFunctions(req);
        decision.returnHost = masterHost;
        return decision;
    }

    if (forceLocal) {
        // We're forced to execute locally here so we do all the messages
        for (int i = 0; i < nMessages; i++) {
            localMessageIdxs.emplace_back(i);
            decision.addMessage(thisHost, req->messages().at(i));
        }
    } else {
        // At this point we know we're the master host, and we've not been
        // asked to force full local execution.

        // Get a list of other registered hosts
        std::set<std::string>& thisRegisteredHosts = registeredHosts[funcStr];

        // For threads/ processes we need to have a snapshot key and be
        // ready to push the snapshot to other hosts.
        // We also have to broadcast the latest snapshots to all registered
        // hosts, regardless of whether they're going to execute a function.
        // This ensures everything is up to date, and we don't have to
        // maintain different records of which hosts hold which updates.
        faabric::util::SnapshotData snapshotData;
        std::string snapshotKey = firstMsg.snapshotkey();
        bool snapshotNeeded = req->type() == req->THREADS ||
                              req->type() == req->PROCESSES ||
                              !snapshotKey.empty();

        if (snapshotNeeded) {
            if (snapshotKey.empty()) {
                SPDLOG_ERROR("No snapshot provided for {}", funcStr);
                throw std::runtime_error(
                  "Empty snapshot for distributed threads/ processes");
            }

            snapshotData =
              faabric::snapshot::getSnapshotRegistry().getSnapshot(snapshotKey);
            if (!thisRegisteredHosts.empty()) {
                std::vector<faabric::util::SnapshotDiff> snapshotDiffs =
                  snapshotData.getDirtyPages();

                // Do the snapshot diff pushing
                if (!snapshotDiffs.empty()) {
                    ZoneScopedN(
                      "Scheduler::callFunctions snapshot diff pushing");
                    for (const auto& h : thisRegisteredHosts) {
                        SPDLOG_DEBUG("Pushing {} snapshot diffs for {} to {} "
                                     "(snapshot size up to {} bytes)",
                                     snapshotDiffs.size(),
                                     funcStr,
                                     h,
                                     snapshotData.size);
                        SnapshotClient& c = getSnapshotClient(h);
                        c.pushSnapshotDiffs(
                          snapshotKey, firstMsg.groupid(), snapshotDiffs);
                    }

                    // Now reset the dirty page tracking, as we want the next
                    // batch of diffs to contain everything from now on
                    // (including the updates sent back from all the threads)
                    SPDLOG_DEBUG(
                      "Resetting dirty tracking after pushing diffs {}",
                      funcStr);
                    faabric::util::resetDirtyTracking();
                }
            }
        }

        // Work out how many we can handle locally
        int nLocally;
        {
            int slots = thisHostResources.slots();

            // Work out available cores, flooring at zero
            int available =
              slots - this->thisHostUsedSlots.load(std::memory_order_acquire);
            available = std::max<int>(available, 0);

            // Claim as many as we can
            nLocally = std::min<int>(available, nMessages);
        }

        // Make sure we don't execute the wrong kind (storage/compute) of
        // call locally
        if (hostKindDifferent) {
            nLocally = 0;
        }

        if (isThreads && nLocally > 0) {
            SPDLOG_DEBUG("Returning {} of {} {} for local threads",
                         nLocally,
                         nMessages,
                         funcStr);
        } else if (nLocally > 0) {
            SPDLOG_DEBUG(
              "Executing {} of {} {} locally", nLocally, nMessages, funcStr);
        } else if (hostKindDifferent) {
            SPDLOG_DEBUG("Requested host kind different, distributing {} x {}",
                         nMessages,
                         funcStr);
        } else {
            SPDLOG_DEBUG(
              "No local capacity, distributing {} x {}", nMessages, funcStr);
        }

        // Add those that can be executed locally
        if (nLocally > 0) {
            SPDLOG_DEBUG(
              "Executing {}/{} {} locally", nLocally, nMessages, funcStr);
            for (int i = 0; i < nLocally; i++) {
                localMessageIdxs.emplace_back(i);
                decision.addMessage(thisHost, req->messages().at(i));
            }
        }

        int offset = nLocally;

        if (hostKindDifferent) {
            std::set<std::string> allHosts =
              getAvailableHostsForFunction(firstMsg);

            if (allHosts.empty()) {
                throw std::runtime_error(
                  "No hosts can execute function requesting different "
                  "kind");
            }

            for (auto& h : allHosts) {
                // Schedule functions on this host
                int nOnThisHost = scheduleFunctionsOnHost(
                  h, req, decision, offset, &snapshotData);

                offset += nOnThisHost;

                // Stop if we've scheduled all functions
                if (offset >= nMessages) {
                    break;
                }
            }

            if (offset < nMessages) {
                // offload the rest onto the first host available
                std::string h = *allHosts.begin();
                int nOnThisHost = scheduleFunctionsOnHost(
                  h, req, decision, offset, &snapshotData, true);

                offset += nOnThisHost;

                if (offset != nMessages) {
                    throw std::logic_error(
                      "Not all other host kind messages could be "
                      "scheduled.");
                }
            }
        }

        // If some are left, we need to distribute
        if (offset < nMessages && !hostKindDifferent) {
            // Schedule first to already registered hosts
            for (const auto& h : thisRegisteredHosts) {
                int nOnThisHost = scheduleFunctionsOnHost(
                  h, req, decision, offset, &snapshotData);

                offset += nOnThisHost;
                if (offset >= nMessages) {
                    break;
                }
            }
        }

        // Now schedule to unregistered hosts if there are some left
        if (offset < nMessages) {
            std::vector<std::string> unregisteredHosts =
              getUnregisteredHosts(funcStr);

            for (auto& h : unregisteredHosts) {
                // Skip if this host
                if (h == thisHost) {
                    continue;
                }

                // Schedule functions on the host
                int nOnThisHost = scheduleFunctionsOnHost(
                  h, req, decision, offset, &snapshotData);

                // Register the host if it's exected a function
                if (nOnThisHost > 0) {
                    SPDLOG_DEBUG("Registering {} for {}", h, funcStr);
                    registeredHosts[funcStr].insert(h);
                }

                offset += nOnThisHost;
                if (offset >= nMessages) {
                    break;
                }
            }
        }

        // At this point there's no more capacity in the system, so we
        // just need to execute locally
        if (offset < nMessages) {
            SPDLOG_DEBUG("Overloading {}/{} {} locally",
                         nMessages - offset,
                         nMessages,
                         funcStr);
            for (; offset < nMessages; offset++) {
                localMessageIdxs.emplace_back(offset);
                decision.addMessage(thisHost, req->messages().at(offset));
            }
        }

        // Sanity check
        assert(offset == nMessages);
    }

    // Register thread results if necessary
    if (isThreads) {
        for (const auto& m : req->messages()) {
            registerThread(m.id());
        }
    }

    // Schedule messages locally if necessary. For threads we only need
    // one executor, for anything else we want one Executor per function
    // in flight
    if (!localMessageIdxs.empty()) {
        ZoneScopedN("Scheduler::callFunctions local execution");
        // Update slots
        this->thisHostUsedSlots.fetch_add((int32_t)localMessageIdxs.size(),
                                          std::memory_order_acquire);

        bool executed = false;
        if (isThreads) {
            // Threads use the existing executor. We assume there's only
            // one running at a time.
            std::vector<std::shared_ptr<Executor>>& thisExecutors =
              executors[funcStr];

            std::shared_ptr<Executor> e = nullptr;
            if (thisExecutors.empty()) {
                ZoneScopedN("Scheduler::callFunctions claiming new executor");
                // Create executor if not exists
                claimExecutor(
                  firstMsg,
                  [req, localMessageIdxs](std::shared_ptr<Executor> exec) {
                      exec->executeTasks(localMessageIdxs, req);
                  });
                executed = true;
            } else if (thisExecutors.size() == 1) {
                // Use existing executor if exists
                e = thisExecutors.back();
            } else {
                SPDLOG_ERROR("Found {} executors for threaded function {}",
                             thisExecutors.size(),
                             funcStr);
                throw std::runtime_error(
                  "Expected only one executor for threaded function");
            }

            assert(e != nullptr);

            // Execute the tasks
            if (!executed) {
                ZoneScopedN("Scheduler::callFunctions execute tasks");
                e->executeTasks(localMessageIdxs, req);
            }
        } else {
            // Non-threads require one executor per task
            for (auto i : localMessageIdxs) {
                faabric::Message& localMsg = req->mutable_messages()->at(i);
                if (localMsg.directresulthost() == config.endpointHost) {
                    localMsg.set_directresulthost("");
                }
                if (localMsg.executeslocally()) {
                    faabric::util::UniqueLock resultsLock(localResultsMutex);
                    localResults.insert(
                      { localMsg.id(),
                        std::make_shared<MessageLocalResult>() });
                }
                ZoneScopedN("Scheduler::callFunctions claim executor");
                claimExecutor(firstMsg,
                              [req, i](std::shared_ptr<Executor> exec) {
                                  exec->executeTasks({ i }, req);
                              });
            }
        }
    }

    // Send out point-to-point mappings if necessary (unless being forced to
    // execute locally, in which case they will be transmitted from the master)
    if (!forceLocal && (firstMsg.groupid() > 0)) {
        broker.setAndSendMappingsFromSchedulingDecision(decision);
    }

    // Records for tests
    if (faabric::util::isTestMode()) {
        for (int i = 0; i < nMessages; i++) {
            std::string executedHost = decision.hosts.at(i);
            faabric::Message msg = req->messages().at(i);

            // Log results if in test mode
            recordedMessagesAll.push_back(msg);
            if (executedHost.empty() || executedHost == thisHost) {
                recordedMessagesLocal.push_back(msg);
            } else {
                recordedMessagesShared.emplace_back(executedHost, msg);
            }
        }
    }

    return decision;
}

std::vector<std::string> Scheduler::getUnregisteredHosts(
  const std::string& funcStr,
  bool noCache)
{
    // Load the list of available hosts
    if (availableHostsCache.empty() || noCache) {
        availableHostsCache = getAvailableHosts();
    }

    // At this point we know we need to enlist unregistered hosts
    std::set<std::string>& thisRegisteredHosts = registeredHosts[funcStr];

    std::vector<std::string> unregisteredHosts;

    std::set_difference(
      availableHostsCache.begin(),
      availableHostsCache.end(),
      thisRegisteredHosts.begin(),
      thisRegisteredHosts.end(),
      std::inserter(unregisteredHosts, unregisteredHosts.begin()));

    // If we've not got any, try again without caching
    if (unregisteredHosts.empty() && !noCache) {
        return getUnregisteredHosts(funcStr, true);
    }

    return unregisteredHosts;
}

void Scheduler::broadcastSnapshotDelete(const faabric::Message& msg,
                                        const std::string& snapshotKey)
{
    std::string funcStr = faabric::util::funcToString(msg, false);
    std::set<std::string>& thisRegisteredHosts = registeredHosts[funcStr];

    for (auto host : thisRegisteredHosts) {
        SnapshotClient& c = getSnapshotClient(host);
        c.deleteSnapshot(snapshotKey);
    }
}

int Scheduler::scheduleFunctionsOnHost(
  const std::string& host,
  std::shared_ptr<faabric::BatchExecuteRequest> req,
  SchedulingDecision& decision,
  int offset,
  faabric::util::SnapshotData* snapshot,
  bool forceAll)
{
    ZoneScopedN("Scheduler::scheduleFunctionsOnHost");
    const faabric::Message& firstMsg = req->messages().at(0);
    std::string funcStr = faabric::util::funcToString(firstMsg, false);

    int nMessages = req->messages_size();
    int remainder = nMessages - offset;

    // Execute as many as possible to this host
    int available{};
    if (forceAll) {
        available = remainder;
    } else {
        faabric::HostResources r = getHostResources(host);
        available = r.slots() - r.usedslots();
    }

    // Drop out if none available
    if (available <= 0) {
        SPDLOG_DEBUG("Not scheduling {} on {}, no resources", funcStr, host);
        return 0;
    }

    // Set up new request
    std::shared_ptr<faabric::BatchExecuteRequest> hostRequest =
      faabric::util::batchExecFactory();
    hostRequest->set_snapshotkey(req->snapshotkey());
    hostRequest->set_type(req->type());
    hostRequest->set_subtype(req->subtype());
    hostRequest->set_contextdata(req->contextdata());

    // Add messages
    int nOnThisHost = std::min<int>(available, remainder);
    for (int i = offset; i < (offset + nOnThisHost); i++) {
        auto* newMsg = hostRequest->add_messages();
        *newMsg = req->messages().at(i);
        newMsg->set_executeslocally(false);
        if (!newMsg->directresulthost().empty()) {
            ZoneScopedN("Create local result promise");
            faabric::util::UniqueLock resultsLock(localResultsMutex);
            localResults.insert(
              { newMsg->id(), std::make_shared<MessageLocalResult>() });
        }
        decision.addMessage(host, req->messages().at(i));
    }

    SPDLOG_DEBUG(
      "Sending {}/{} {} to {}", nOnThisHost, nMessages, funcStr, host);

    // Handle snapshots
    std::string snapshotKey = firstMsg.snapshotkey();
    if (snapshot != nullptr && !snapshotKey.empty()) {
        SnapshotClient& c = getSnapshotClient(host);
        c.pushSnapshot(snapshotKey, firstMsg.groupid(), *snapshot);
    }

    getFunctionCallClient(host).executeFunctions(hostRequest);

    return nOnThisHost;
}

void Scheduler::callFunction(faabric::Message& msg,
                             bool forceLocal,
                             faabric::Message* caller)
{
    // TODO - avoid this copy
    auto req = faabric::util::batchExecFactory();
    *req->add_messages() = msg;

    // Specify that this is a normal function, not a thread
    req->set_type(req->FUNCTIONS);

    // Make the call
    callFunctions(req, forceLocal, caller);
}

void Scheduler::clearRecordedMessages()
{
    recordedMessagesAll.clear();
    recordedMessagesLocal.clear();
    recordedMessagesShared.clear();
}

std::vector<faabric::Message> Scheduler::getRecordedMessagesAll()
{
    return recordedMessagesAll;
}

std::vector<faabric::Message> Scheduler::getRecordedMessagesLocal()
{
    return recordedMessagesLocal;
}

FunctionCallClient& Scheduler::getFunctionCallClient(
  const std::string& otherHost)
{
    if (functionCallClients.find(otherHost) == functionCallClients.end()) {
        SPDLOG_DEBUG("Adding new function call client for {}", otherHost);
        functionCallClients.emplace(otherHost, otherHost);
    }

    return functionCallClients.at(otherHost);
}

SnapshotClient& Scheduler::getSnapshotClient(const std::string& otherHost)
{
    if (snapshotClients.find(otherHost) == snapshotClients.end()) {
        SPDLOG_DEBUG("Adding new snapshot client for {}", otherHost);
        snapshotClients.emplace(otherHost, otherHost);
    }

    return snapshotClients.at(otherHost);
}

std::vector<std::pair<std::string, faabric::Message>>
Scheduler::getRecordedMessagesShared()
{
    return recordedMessagesShared;
}

void Scheduler::claimExecutor(
  faabric::Message& msg,
  std::function<void(std::shared_ptr<Executor>)> runOnExecutor)
{
    std::string funcStr = faabric::util::funcToString(msg, false);

    std::vector<std::shared_ptr<Executor>>& thisExecutors = executors[funcStr];

    if (thisExecutors.empty()) {
        suspendedExecutors[funcStr] = 0;
    }

    std::shared_ptr<Executor> claimed = nullptr;
    for (auto& e : thisExecutors) {
        if (e->tryClaim()) {
            claimed = e;
            SPDLOG_DEBUG(
              "Reusing warm executor {} for {}", claimed->id, funcStr);
            break;
        }
    }

    // We have no warm executors available, so scale up
    if (claimed == nullptr) {
        int nExecutors = thisExecutors.size();
        int nSuspended = suspendedExecutors[funcStr];
        // allow for 2 threads per available core, 12 threads in case of
        // suspended threads
        int maxSubscription = 2 * std::thread::hardware_concurrency();
        if (nExecutors - std::min(nSuspended, maxSubscription * 6) >
            std::max(1, maxSubscription)) {
            ZoneScopedN("Scheduler::claimExecutor oversubscribed");
            // oversubscribed, enqueue onto one of the other executors
            int minQueueSize = thisExecutors.at(0)->getQueueLength();
            int minQueueIdx = 0;
            for (int i = 1; i < nExecutors && minQueueSize > 0; i++) {
                int qs = thisExecutors.at(i)->getQueueLength();
                if (qs < minQueueSize) {
                    minQueueSize = qs;
                    minQueueIdx = i;
                }
            }
            SPDLOG_DEBUG("Queueing {} onto oversubscribed executor {}",
                         funcStr,
                         minQueueIdx);
            claimed = thisExecutors.at(minQueueIdx);
        } else {
            ZoneScopedN("Scheduler::claimExecutor scaling up");
            SPDLOG_DEBUG(
              "Scaling {} from {} -> {}", funcStr, nExecutors, nExecutors + 1);
            std::shared_ptr<faabric::scheduler::ExecutorFactory> factory =
              getExecutorFactory();
            auto executor = factory->createExecutor(msg);
            executor->tryClaim();
            thisExecutors.push_back(std::move(executor));
            claimed = thisExecutors.back();
        }
    }

    assert(claimed != nullptr);
    runOnExecutor(claimed);
}

std::string Scheduler::getThisHost()
{
    return thisHost;
}

void Scheduler::broadcastFlush()
{
    // Get all hosts
    redis::Redis& redis = redis::Redis::getQueue();
    std::set<std::string> allHosts = redis.smembers(AVAILABLE_HOST_SET);
    allHosts.merge(redis.smembers(AVAILABLE_STORAGE_HOST_SET));

    // Remove this host from the set
    allHosts.erase(thisHost);

    // Dispatch flush message to all other hosts
    for (auto& otherHost : allHosts) {
        getFunctionCallClient(otherHost).sendFlush();
    }

    flushLocally();
}

void Scheduler::flushLocally()
{
    SPDLOG_INFO("Flushing host {}",
                faabric::util::getSystemConfig().endpointHost);

    // Reset this scheduler
    reset();

    // Flush the host
    getExecutorFactory()->flushHost();
}

void Scheduler::setFunctionResult(faabric::Message& msg)
{
    ZoneScopedNS("Scheduler::setFunctionResult", 5);
    ZoneValue(msg.ByteSizeLong());

    const auto& myHostname = faabric::util::getSystemConfig().endpointHost;

    const auto& directResultHost = msg.directresulthost();
    if (directResultHost == myHostname) {
        faabric::util::UniqueLock resultsLock(localResultsMutex);
        auto it = localResults.find(msg.id());
        if (it != localResults.end()) {
            it->second->set_value(std::make_unique<faabric::Message>(msg));
        } else {
            throw std::runtime_error(
              "Got direct result, but promise is registered");
        }
        return;
    }

    // Record which host did the execution
    msg.set_executedhost(myHostname);

    // Set finish timestamp
    msg.set_finishtimestamp(faabric::util::getGlobalClock().epochMillis());

    if (!directResultHost.empty()) {
        ZoneScopedN("Direct result send");
        faabric::util::FullLock lock(mx);
        auto& fc = getFunctionCallClient(directResultHost);
        lock.unlock();
        {
            ZoneScopedN("Socket send");
            fc.sendDirectResult(msg);
        }
        return;
    }

    if (msg.executeslocally()) {
        ZoneScopedN("Local results publish");
        faabric::util::UniqueLock resultsLock(localResultsMutex);
        auto it = localResults.find(msg.id());
        if (it != localResults.end()) {
            it->second->set_value(std::make_unique<faabric::Message>(msg));
        }
        return;
    }

    std::string key = msg.resultkey();
    if (key.empty()) {
        throw std::runtime_error("Result key empty. Cannot publish result");
    }

    // Write the successful result to the result queue
    std::vector<uint8_t> inputData = faabric::util::messageToBytes(msg);
    redis::Redis& redis = redis::Redis::getQueue();
    redis.publishSchedulerResult(key, msg.statuskey(), inputData);
}

void Scheduler::registerThread(uint32_t msgId)
{
    // Here we need to ensure the promise is registered locally so
    // callers can start waiting
    threadResults[msgId];
}

void Scheduler::setThreadResult(const faabric::Message& msg,
                                int32_t returnValue)
{
    bool isMaster = msg.masterhost() == conf.endpointHost;

    if (isMaster) {
        setThreadResultLocally(msg.id(), returnValue);
    } else {
        SnapshotClient& c = getSnapshotClient(msg.masterhost());
        c.pushThreadResult(msg.id(), returnValue);
    }
}

void Scheduler::pushSnapshotDiffs(
  const faabric::Message& msg,
  const std::vector<faabric::util::SnapshotDiff>& diffs)
{
    bool isMaster = msg.masterhost() == conf.endpointHost;

    if (!isMaster && !diffs.empty()) {
        SnapshotClient& c = getSnapshotClient(msg.masterhost());
        c.pushSnapshotDiffs(msg.snapshotkey(), msg.groupid(), diffs);
    }
}

void Scheduler::setThreadResultLocally(uint32_t msgId, int32_t returnValue)
{
    SPDLOG_DEBUG("Setting result for thread {} to {}", msgId, returnValue);
    threadResults[msgId].set_value(returnValue);
}

int32_t Scheduler::awaitThreadResult(uint32_t messageId)
{
    if (threadResults.count(messageId) == 0) {
        SPDLOG_ERROR("Thread {} not registered on this host", messageId);
        throw std::runtime_error("Awaiting unregistered thread");
    }

    return threadResults[messageId].get_future().get();
}

faabric::Message Scheduler::getFunctionResult(unsigned int messageId,
                                              int timeoutMs,
                                              faabric::Message* caller)
{
    std::atomic_int* suspendedCtr = nullptr;
    if (caller != nullptr) {
        faabric::util::SharedLock _l(mx);
        suspendedCtr =
          &suspendedExecutors[faabric::util::funcToString(*caller, false)];
        _l.unlock();
        suspendedCtr->fetch_add(1, std::memory_order_acq_rel);
        monitorWaitingTasks.fetch_add(1, std::memory_order_acq_rel);
    }
    struct SuspendedGuard
    {
        std::atomic_int* ctr;
        ~SuspendedGuard()
        {
            if (ctr != nullptr) {
                getScheduler().monitorWaitingTasks.fetch_sub(
                  1, std::memory_order_acq_rel);
                ctr->fetch_sub(1, std::memory_order_acq_rel);
                ctr = nullptr;
            }
        }
    } suspendedGuard{ suspendedCtr };

    ZoneScopedNS("Scheduler::getFunctionResult", 5);
    ZoneValue(messageId);

    bool isBlocking = timeoutMs > 0;

    if (messageId == 0) {
        throw std::runtime_error("Must provide non-zero message ID");
    }

    do {
        std::future<std::unique_ptr<faabric::Message>> fut;
        {
            faabric::util::UniqueLock resultsLock(localResultsMutex);
            auto it = localResults.find(messageId);
            if (it == localResults.end()) {
                break; // fallback to redis
            }
            fut = it->second->promise.get_future();
        }
        if (!isBlocking) {
            ZoneScopedNS("Wait for future", 5);
            auto status = fut.wait_for(std::chrono::milliseconds(timeoutMs));
            if (status == std::future_status::timeout) {
                faabric::Message msgResult;
                msgResult.set_type(faabric::Message_MessageType_EMPTY);
                return msgResult;
            }
        } else {
            ZoneScopedNS("Wait for future", 5);
            fut.wait();
        }
        ZoneNamedNS(_zone_grab, "Grab future", 5, true);
        {
            faabric::util::UniqueLock resultsLock(localResultsMutex);
            localResults.erase(messageId);
        }
        return *fut.get();
    } while (0);

    redis::Redis& redis = redis::Redis::getQueue();
    TracyMessageL("Got redis queue");

    std::string resultKey = faabric::util::resultKeyFromMessageId(messageId);

    faabric::Message msgResult;

    if (isBlocking) {
        // Blocking version will throw an exception when timing out
        // which is handled by the caller.
        TracyMessageL("Dequeueing bytes");
        std::vector<uint8_t> result = redis.dequeueBytes(resultKey, timeoutMs);
        ZoneScopedN("Parse result message");
        msgResult.ParseFromArray(result.data(), (int)result.size());
    } else {
        // Non-blocking version will tolerate empty responses, therefore
        // we handle the exception here
        std::vector<uint8_t> result;
        try {
            result = redis.dequeueBytes(resultKey, timeoutMs);
        } catch (redis::RedisNoResponseException& ex) {
            // Ok for no response when not blocking
        }

        if (result.empty()) {
            // Empty result has special type
            msgResult.set_type(faabric::Message_MessageType_EMPTY);
        } else {
            // Normal response if we get something from redis
            ZoneScopedN("Parse result message");
            msgResult.ParseFromArray(result.data(), (int)result.size());
        }
    }

    return msgResult;
}

void Scheduler::getFunctionResultAsync(
  unsigned int messageId,
  int timeoutMs,
  asio::io_context& ioc,
  asio::any_io_executor& executor,
  std::function<void(faabric::Message&)> handler)
{
    ZoneScopedNS("Scheduler::getFunctionResultAsync", 5);
    ZoneValue(messageId);

    if (messageId == 0) {
        throw std::runtime_error("Must provide non-zero message ID");
    }

    do {
        std::shared_ptr<MessageLocalResult> mlr;
        {
            faabric::util::UniqueLock resultsLock(localResultsMutex);
            auto it = localResults.find(messageId);
            if (it == localResults.end()) {
                break; // fallback to redis
            }
            mlr = it->second;
        }
        struct MlrAwaiter : public std::enable_shared_from_this<MlrAwaiter>
        {
            unsigned int messageId;
            Scheduler* sched;
            std::shared_ptr<MessageLocalResult> mlr;
            asio::posix::stream_descriptor dsc;
            std::function<void(faabric::Message&)> handler;
            MlrAwaiter(unsigned int messageId,
                       Scheduler* sched,
                       std::shared_ptr<MessageLocalResult> mlr,
                       asio::posix::stream_descriptor dsc,
                       std::function<void(faabric::Message&)> handler)
              : messageId(messageId)
              , sched(sched)
              , mlr(std::move(mlr))
              , dsc(std::move(dsc))
              , handler(handler)
            {}
            ~MlrAwaiter() { dsc.release(); }
            void await(const boost::system::error_code& ec)
            {
                if (!ec) {
                    auto msg = mlr->promise.get_future().get();
                    handler(*msg);
                    {
                        faabric::util::UniqueLock resultsLock(
                          sched->localResultsMutex);
                        sched->localResults.erase(messageId);
                    }
                } else {
                    doAwait();
                }
            }
            void doAwait()
            {
                dsc.async_wait(asio::posix::stream_descriptor::wait_read,
                               beast::bind_front_handler(
                                 &MlrAwaiter::await, this->shared_from_this()));
            }
        };
        auto awaiter = std::make_shared<MlrAwaiter>(
          messageId,
          this,
          mlr,
          asio::posix::stream_descriptor(ioc, mlr->event_fd),
          std::move(handler));
        awaiter->doAwait();
        return;
    } while (0);

    // TODO: Non-blocking redis
    redis::Redis& redis = redis::Redis::getQueue();
    TracyMessageL("Got redis queue");

    std::string resultKey = faabric::util::resultKeyFromMessageId(messageId);

    faabric::Message msgResult;

    // Blocking version will throw an exception when timing out
    // which is handled by the caller.
    TracyMessageL("Dequeueing bytes");
    std::vector<uint8_t> result = redis.dequeueBytes(resultKey, timeoutMs);
    {
        ZoneScopedN("Parse result message");
        msgResult.ParseFromArray(result.data(), (int)result.size());
    }

    handler(msgResult);
}

faabric::HostResources Scheduler::getThisHostResources()
{
    thisHostResources.set_usedslots(
      this->thisHostUsedSlots.load(std::memory_order_acquire));
    return thisHostResources;
}

void Scheduler::setThisHostResources(faabric::HostResources& res)
{
    thisHostResources = res;
    this->thisHostUsedSlots.store(res.usedslots(), std::memory_order_release);
}

faabric::HostResources Scheduler::getHostResources(const std::string& host)
{
    return getFunctionCallClient(host).getResources();
}

// --------------------------------------------
// EXECUTION GRAPH
// --------------------------------------------

#define CHAINED_SET_PREFIX "chained_"
std::string getChainedKey(unsigned int msgId)
{
    return std::string(CHAINED_SET_PREFIX) + std::to_string(msgId);
}

void Scheduler::logChainedFunction(unsigned int parentMessageId,
                                   unsigned int chainedMessageId)
{
    redis::Redis& redis = redis::Redis::getQueue();

    const std::string& key = getChainedKey(parentMessageId);
    redis.sadd(key, std::to_string(chainedMessageId));
    redis.expire(key, STATUS_KEY_EXPIRY);
}

std::set<unsigned int> Scheduler::getChainedFunctions(unsigned int msgId)
{
    redis::Redis& redis = redis::Redis::getQueue();

    const std::string& key = getChainedKey(msgId);
    const std::set<std::string> chainedCalls = redis.smembers(key);

    std::set<unsigned int> chainedIds;
    for (auto i : chainedCalls) {
        chainedIds.insert(std::stoi(i));
    }

    return chainedIds;
}

void Scheduler::updateMonitoring()
{
    if (this->monitorFd < 0) {
        return;
    }
    static std::mutex monitorMx;
    std::unique_lock<std::mutex> monitorLock(monitorMx);
    thread_local std::string wrBuffer = std::string(size_t(128), char('\0'));
    wrBuffer.clear();
    constexpr auto ord = std::memory_order_acq_rel;
    int32_t locallySched = monitorLocallyScheduledTasks.load(ord);
    int32_t started = monitorStartedTasks.load(ord);
    int32_t waiting = monitorWaitingTasks.load(ord);
    fmt::format_to(
      std::back_inserter(wrBuffer),
      "local_sched,{},waiting_queued,{},started,{},waiting,{},active,{}\n",
      locallySched,
      locallySched - started,
      started,
      waiting,
      started - waiting);
    const size_t size = wrBuffer.size();
    flock(monitorFd, LOCK_EX);
    ftruncate(monitorFd, size);
    lseek(monitorFd, 0, SEEK_SET);
    ssize_t pos = 0;
    while (pos < size) {
        ssize_t written = write(monitorFd, wrBuffer.data() + pos, size - pos);
        if (written < 0 && errno != EAGAIN) {
            perror("Couldn't write monitoring data");
        }
        if (written == 0) {
            SPDLOG_WARN("Couldn't write monitoring data");
            break;
        }
        if (written > 0) {
            pos += written;
        }
    }
    flock(monitorFd, LOCK_UN);
}

ExecGraph Scheduler::getFunctionExecGraph(unsigned int messageId)
{
    ExecGraphNode rootNode = getFunctionExecGraphNode(messageId);
    ExecGraph graph{ .rootNode = rootNode };

    return graph;
}

ExecGraphNode Scheduler::getFunctionExecGraphNode(unsigned int messageId)
{
    redis::Redis& redis = redis::Redis::getQueue();

    // Get the result for this message
    std::string statusKey = faabric::util::statusKeyFromMessageId(messageId);
    std::vector<uint8_t> messageBytes = redis.get(statusKey);
    faabric::Message result;
    result.ParseFromArray(messageBytes.data(), (int)messageBytes.size());

    // Recurse through chained calls
    std::set<unsigned int> chainedMsgIds = getChainedFunctions(messageId);
    std::vector<ExecGraphNode> children;
    for (auto c : chainedMsgIds) {
        children.emplace_back(getFunctionExecGraphNode(c));
    }

    // Build the node
    ExecGraphNode node{ .msg = result, .children = children };

    return node;
}
}
